#include "overlay.h"
#include "renderer.h"
#include "config.h"
#include "resource.h"
#include <random>

static const wchar_t* OVERLAY_CLASS = L"SimpleScreenMarkOverlay";

// mt19937 supports full int range; rand() is capped at RAND_MAX=32767
static std::mt19937 g_rng{std::random_device{}()};

// Generate random float in [-range/2, +range/2]
static float RandomOffset(int range) {
    if (range <= 0) return 0.0f;
    std::uniform_int_distribution<int> dist(-range / 2, range / 2);
    return (float)dist(g_rng);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// After setting our overlays topmost, push the shell taskbar windows back above
// them so the taskbar is never obscured by our overlay.
// Shell_TrayWnd       = primary taskbar
// Shell_SecondaryTrayWnd = per-monitor taskbar (multi-monitor, Windows 11)
static void ReassertTaskbarZOrder() {
    HWND h = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (h) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void RegisterOverlayClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = OVERLAY_CLASS;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

struct EnumMonitorData {
    std::vector<RECT> monitors;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    auto data = (EnumMonitorData*)lParam;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        data->monitors.push_back(mi.rcMonitor);
    }
    return TRUE;
}

// Explorer treats a monitor-sized TOPMOST HWND as a fullscreen application and
// changes the taskbar's z-order.  A one-pixel gap is not reliable here: after an
// RDP reconnect/resolution change, the shell may use a different tolerance while
// its display topology is settling.  Cover the monitor with two adjacent windows
// instead.  The visual result is unchanged, but no individual top-level window
// has fullscreen dimensions for Explorer to classify as a fullscreen app.
static std::vector<RECT> ComputeOverlayTiles(const RECT& monitor) {
    std::vector<RECT> tiles;
    const int width = monitor.right - monitor.left;
    const int height = monitor.bottom - monitor.top;

    if (height >= 2) {
        const LONG middle = monitor.top + height / 2;
        tiles.push_back({monitor.left, monitor.top, monitor.right, middle});
        tiles.push_back({monitor.left, middle, monitor.right, monitor.bottom});
    } else if (width >= 2) {
        const LONG middle = monitor.left + width / 2;
        tiles.push_back({monitor.left, monitor.top, middle, monitor.bottom});
        tiles.push_back({middle, monitor.top, monitor.right, monitor.bottom});
    } else if (width > 0 && height > 0) {
        tiles.push_back(monitor);
    }

    return tiles;
}

static void ApplyLayeredBitmap(HWND hwnd, HBITMAP hBitmap,
                               const RECT& windowArea, const RECT& monitorArea) {
    int width  = windowArea.right - windowArea.left;
    int height = windowArea.bottom - windowArea.top;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);

    POINT ptSrc = {
        windowArea.left - monitorArea.left,
        windowArea.top - monitorArea.top
    };
    SIZE sz = {width, height};
    POINT ptDst = {windowArea.left, windowArea.top};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sz,
                        hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void CreateOverlays(HINSTANCE hInstance, const Config& cfg,
                    const wchar_t* resolvedText,
                    std::vector<OverlayWindow>& overlays) {
    DestroyOverlays(overlays);

    // Enumerate monitors
    EnumMonitorData data;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&data);

    for (const RECT& monitor : data.monitors) {
        const int monitorWidth = monitor.right - monitor.left;
        const int monitorHeight = monitor.bottom - monitor.top;
        float ox = RandomOffset(cfg.randomOffsetX);
        float oy = RandomOffset(cfg.randomOffsetY);
        HBITMAP hBitmap = RenderWatermark(
            cfg, monitorWidth, monitorHeight, resolvedText, ox, oy);

        for (const RECT& tile : ComputeOverlayTiles(monitor)) {
            const int width = tile.right - tile.left;
            const int height = tile.bottom - tile.top;
            HWND hwnd = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                OVERLAY_CLASS, L"",
                WS_POPUP,
                tile.left, tile.top, width, height,
                nullptr, nullptr, hInstance, nullptr);

            if (!hwnd) continue;

            if (hBitmap) {
                ApplyLayeredBitmap(hwnd, hBitmap, tile, monitor);
            }

            OverlayWindow ow;
            ow.hwnd = hwnd;
            ow.windowArea = tile;
            ow.monitorArea = monitor;
            overlays.push_back(ow);
        }

        if (hBitmap) DeleteObject(hBitmap);
    }

    // Z-order: taskbar above any other topmost competitor (e.g. WeChat),
    // then our overlay above the taskbar. The overlay is WS_EX_TRANSPARENT so
    // the taskbar remains fully clickable. Tiling prevents Explorer from
    // mistaking an overlay HWND for a fullscreen application.
    ReassertTaskbarZOrder();
    for (auto& ow : overlays) {
        if (ow.hwnd && IsWindow(ow.hwnd)) {
            SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
}

void UpdateOverlays(const Config& cfg, const wchar_t* resolvedText,
                    std::vector<OverlayWindow>& overlays) {
    size_t first = 0;
    while (first < overlays.size()) {
        const RECT monitor = overlays[first].monitorArea;
        const int width = monitor.right - monitor.left;
        const int height = monitor.bottom - monitor.top;
        float ox = RandomOffset(cfg.randomOffsetX);
        float oy = RandomOffset(cfg.randomOffsetY);
        HBITMAP hBitmap = RenderWatermark(cfg, width, height, resolvedText, ox, oy);

        size_t next = first;
        while (next < overlays.size() &&
               EqualRect(&overlays[next].monitorArea, &monitor)) {
            auto& ow = overlays[next];
            if (hBitmap && ow.hwnd && IsWindow(ow.hwnd)) {
                ApplyLayeredBitmap(
                    ow.hwnd, hBitmap, ow.windowArea, ow.monitorArea);
            }
            ++next;
        }
        if (hBitmap) DeleteObject(hBitmap);

        // No SetWindowPos here — z-order is established at create/show time.
        // Re-asserting TOPMOST on every content update would repeatedly push
        // the overlay above the taskbar, causing taskbar z-order interference.
        first = next;
    }
}

void ShowOverlays(std::vector<OverlayWindow>& overlays, bool show,
                  const Config* cfg, const wchar_t* resolvedText) {
    if (show) {
        // Refresh content first (time/ip may have changed while hidden)
        if (cfg && resolvedText) {
            UpdateOverlays(*cfg, resolvedText, overlays);
        }
        // Taskbar above competitors, overlay above taskbar
        ReassertTaskbarZOrder();
        for (auto& ow : overlays) {
            if (ow.hwnd && IsWindow(ow.hwnd)) {
                SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }
    } else {
        for (auto& ow : overlays) {
            if (ow.hwnd && IsWindow(ow.hwnd)) {
                ShowWindow(ow.hwnd, SW_HIDE);
            }
        }
    }
}

void DestroyOverlays(std::vector<OverlayWindow>& overlays) {
    for (auto& ow : overlays) {
        if (ow.hwnd && IsWindow(ow.hwnd)) {
            DestroyWindow(ow.hwnd);
        }
    }
    overlays.clear();
}

void ReassertOverlayZOrder(std::vector<OverlayWindow>& overlays) {
    // Taskbar above competitors first, then overlay above taskbar
    ReassertTaskbarZOrder();
    for (auto& ow : overlays) {
        if (ow.hwnd && IsWindow(ow.hwnd) && IsWindowVisible(ow.hwnd)) {
            SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

bool OverlaysMatchCurrentMonitors(const std::vector<OverlayWindow>& overlays) {
    EnumMonitorData data;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&data);

    size_t expectedCount = 0;
    for (const RECT& monitor : data.monitors) {
        const auto tiles = ComputeOverlayTiles(monitor);
        expectedCount += tiles.size();

        for (const RECT& tile : tiles) {
            bool found = false;
            for (const auto& overlay : overlays) {
                RECT actual = {};
                if (overlay.hwnd && IsWindow(overlay.hwnd) &&
                    GetWindowRect(overlay.hwnd, &actual) &&
                    EqualRect(&overlay.monitorArea, &monitor) &&
                    EqualRect(&overlay.windowArea, &tile) &&
                    EqualRect(&actual, &tile)) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
    }

    return overlays.size() == expectedCount;
}
