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
    std::vector<RECT> workAreas;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    auto data = (EnumMonitorData*)lParam;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        data->workAreas.push_back(mi.rcWork);
    }
    return TRUE;
}

static void ApplyLayeredBitmap(HWND hwnd, HBITMAP hBitmap, const RECT& workArea) {
    int width  = workArea.right - workArea.left;
    int height = workArea.bottom - workArea.top;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);

    POINT ptSrc = {0, 0};
    SIZE sz = {width, height};
    POINT ptDst = {workArea.left, workArea.top};
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

    for (const auto& workArea : data.workAreas) {
        int width  = workArea.right - workArea.left;
        int height = workArea.bottom - workArea.top;

        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            OVERLAY_CLASS, L"",
            WS_POPUP,
            workArea.left, workArea.top, width, height,
            nullptr, nullptr, hInstance, nullptr);

        if (!hwnd) continue;

        // Render watermark bitmap with random offsets
        float ox = RandomOffset(cfg.randomOffsetX);
        float oy = RandomOffset(cfg.randomOffsetY);
        HBITMAP hBitmap = RenderWatermark(cfg, width, height, resolvedText, ox, oy);
        if (hBitmap) {
            ApplyLayeredBitmap(hwnd, hBitmap, workArea);
            DeleteObject(hBitmap);
        }

        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

        OverlayWindow ow;
        ow.hwnd = hwnd;
        ow.workArea = workArea;
        overlays.push_back(ow);
    }

    // Push taskbar back above our overlays
    ReassertTaskbarZOrder();
}

void UpdateOverlays(const Config& cfg, const wchar_t* resolvedText,
                    std::vector<OverlayWindow>& overlays) {
    for (auto& ow : overlays) {
        if (!ow.hwnd || !IsWindow(ow.hwnd)) continue;

        int width  = ow.workArea.right - ow.workArea.left;
        int height = ow.workArea.bottom - ow.workArea.top;

        float ox = RandomOffset(cfg.randomOffsetX);
        float oy = RandomOffset(cfg.randomOffsetY);
        HBITMAP hBitmap = RenderWatermark(cfg, width, height, resolvedText, ox, oy);
        if (hBitmap) {
            ApplyLayeredBitmap(ow.hwnd, hBitmap, ow.workArea);
            DeleteObject(hBitmap);
        }
        // No SetWindowPos here — z-order is established at create/show time.
        // Re-asserting TOPMOST on every content update would repeatedly push
        // the overlay above the taskbar, causing taskbar z-order interference.
    }
}

void ShowOverlays(std::vector<OverlayWindow>& overlays, bool show,
                  const Config* cfg, const wchar_t* resolvedText) {
    if (show) {
        // Refresh content first (time/ip may have changed while hidden)
        if (cfg && resolvedText) {
            UpdateOverlays(*cfg, resolvedText, overlays);
        }
        for (auto& ow : overlays) {
            if (ow.hwnd && IsWindow(ow.hwnd)) {
                SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }
        // Push taskbar back above our overlays
        ReassertTaskbarZOrder();
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
    for (auto& ow : overlays) {
        if (ow.hwnd && IsWindow(ow.hwnd) && IsWindowVisible(ow.hwnd)) {
            SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    ReassertTaskbarZOrder();
}
