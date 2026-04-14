#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <ctime>
#include <random>

#include "resource.h"
#include "config.h"
#include "overlay.h"
#include "renderer.h"
#include "trayicon.h"
#include "hotkey.h"
#include "netinfo.h"
#include "autostart.h"

#pragma comment(lib, "gdiplus.lib")

// Global state
static HINSTANCE    g_hInstance     = nullptr;
static HWND         g_hwndMain     = nullptr;
static Config       g_config       = {};
static bool         g_watermarkVisible = true;
static std::wstring g_lastResolvedText;
static ULONG_PTR    g_gdiplusToken = 0;

static std::vector<OverlayWindow> g_overlays;

// WinEvent hook — detects other windows going topmost so we can re-assert ours
static HWINEVENTHOOK g_zorderHook = nullptr;

// Shared RNG — mt19937 supports full int range unlike rand() (RAND_MAX=32767)
static std::mt19937 g_rng{std::random_device{}()};

// Forward declarations
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static std::wstring ResolveTemplate(const Config& cfg);
static void RefreshOverlays();
static void RecreateOverlays();
static void ToggleWatermark();
static void TryRegisterHotkey();
static void InstallZOrderHook();
static void RemoveZOrderHook();

// Convert modifiers+vk to human-readable string, e.g. "Ctrl+W"
static std::wstring HotkeyToString(UINT modifiers, UINT vk) {
    std::wstring s;
    if (modifiers & MOD_CONTROL) s += L"Ctrl+";
    if (modifiers & MOD_ALT)     s += L"Alt+";
    if (modifiers & MOD_SHIFT)   s += L"Shift+";
    if (modifiers & MOD_WIN)     s += L"Win+";
    if (vk >= 'A' && vk <= 'Z') {
        s += (wchar_t)vk;
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        wchar_t buf[8];
        swprintf_s(buf, L"F%d", vk - VK_F1 + 1);
        s += buf;
    } else {
        wchar_t buf[16];
        swprintf_s(buf, L"VK(0x%02X)", vk);
        s += buf;
    }
    return s;
}

// Show hotkey-error balloon (called via one-shot timer so tray icon is ready)
static void ShowHotkeyErrorBalloon() {
    std::wstring hotkey = HotkeyToString(g_config.hotkeyModifiers, g_config.hotkeyVk);
    std::wstring msg =
        L"Hotkey [" + hotkey + L"] failed to register.\n"
        L"Another program may be using it.\n"
        L"Edit config.ini (Hotkey=) to change it.";
    ShowBalloonTip(g_hwndMain, L"SimpleScreenMark - Hotkey Error", msg.c_str(), NIIF_WARNING);
}

static void TryRegisterHotkey() {
    UnregisterToggleHotkey(g_hwndMain);   // safe even if not registered
    if (!RegisterToggleHotkey(g_hwndMain, g_config.hotkeyModifiers, g_config.hotkeyVk)) {
        // Delay the balloon by 600ms so tray icon has time to appear
        SetTimer(g_hwndMain, TIMER_HOTKEY_NOTIFY, 600, nullptr);
    }
}

// Returns true if any popup menu is currently visible above the taskbar.
// Covers both standard Win32 menus (class #32768) and custom popup windows
// (e.g. WeChat / QQ player context menus built with custom UI frameworks).
//
// Detection strategy: walk the TOPMOST z-order band from the top. Any visible
// WS_POPUP window that is (a) not our overlay, (b) not the taskbar, and
// (c) small enough to be a menu rather than a full app window is treated as a
// transient popup. While such a popup is visible we skip ReassertTaskbarZOrder()
// so the taskbar does not cover it.
static bool IsAnyMenuVisible() {
    // 1. Standard Win32 menus (always class #32768)
    {
        HWND h = nullptr;
        while ((h = FindWindowExW(nullptr, h, L"#32768", nullptr)) != nullptr)
            if (IsWindowVisible(h)) return true;
    }

    // 2. Custom popup menus: scan the TOPMOST band top-down
    for (HWND w = GetTopWindow(nullptr); w; w = GetNextWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w)) continue;

        LONG exStyle = GetWindowLongW(w, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_TOPMOST)) break;   // left TOPMOST band — stop

        // Skip our own overlay windows
        bool isOurs = false;
        for (const auto& ow : g_overlays)
            if (ow.hwnd == w) { isOurs = true; break; }
        if (isOurs) continue;

        // Skip taskbar
        wchar_t cls[64] = {};
        GetClassNameW(w, cls, 64);
        if (wcscmp(cls, L"Shell_TrayWnd") == 0 ||
            wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) continue;

        // Small WS_POPUP → likely a custom context menu, not a full app window.
        // Threshold 800×800 covers typical menus (< 500 wide) while excluding
        // persistent app windows (WeChat, etc. are wider / taller).
        LONG style = GetWindowLongW(w, GWL_STYLE);
        if (style & WS_POPUP) {
            RECT r;
            GetWindowRect(w, &r);
            if ((r.right - r.left) < 800 && (r.bottom - r.top) < 800)
                return true;
        }
    }

    return false;
}

// WinEvent callback — fires when any window's Z-order changes.
// We use EVENT_OBJECT_REORDER which covers SetWindowPos z-order changes,
// including when another app calls SetWindowPos(HWND_TOPMOST).
// Re-assert our overlay above all normal topmost windows (but keep taskbar on top).
static void CALLBACK ZOrderEventProc(HWINEVENTHOOK, DWORD event,
                                      HWND hwndChanged, LONG idObject,
                                      LONG, DWORD, DWORD) {
    // Only care about window-level reorders (not child controls, menus, etc.)
    if (idObject != OBJID_WINDOW) return;

    // Ignore our own overlay windows to avoid an infinite re-assertion loop
    for (const auto& ow : g_overlays) {
        if (ow.hwnd == hwndChanged) return;
    }

    // Only re-assert when watermark is visible
    if (!g_watermarkVisible) return;

    // Post a message so the re-assertion runs on the main thread
    // (WinEvent callbacks must not call SendMessage to windows in other threads)
    PostMessage(g_hwndMain, WM_APP + 3, 0, 0);
}

static void InstallZOrderHook() {
    if (g_zorderHook) return;
    // WINEVENT_OUTOFCONTEXT: callback runs in our process via message pump,
    // no cross-process injection needed.
    g_zorderHook = SetWinEventHook(
        EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER,
        nullptr, ZOrderEventProc,
        0, 0,  // all processes and threads
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

static void RemoveZOrderHook() {
    if (g_zorderHook) {
        UnhookWinEvent(g_zorderHook);
        g_zorderHook = nullptr;
    }
}

static std::wstring ResolveTemplate(const Config& cfg) {
    std::wstring text = cfg.templateText;

    // Replace {hostname}
    size_t pos;
    while ((pos = text.find(L"{hostname}")) != std::wstring::npos) {
        text.replace(pos, 10, GetHostname());
    }

    // Replace {ip}
    while ((pos = text.find(L"{ip}")) != std::wstring::npos) {
        text.replace(pos, 4, GetIPAddress(cfg.nic));
    }

    // Replace {time}
    while ((pos = text.find(L"{time}")) != std::wstring::npos) {
        time_t now = time(nullptr);
        struct tm tmLocal;
        localtime_s(&tmLocal, &now);
        wchar_t timeBuf[256];
        wcsftime(timeBuf, _countof(timeBuf), cfg.timeFormat.c_str(), &tmLocal);
        text.replace(pos, 6, timeBuf);
    }

    return text;
}

static bool HasDynamicOffset() {
    return g_config.randomOffsetX > 0 || g_config.randomOffsetY > 0;
}

static void RefreshOverlays() {
    std::wstring resolved = ResolveTemplate(g_config);
    // Always re-render if dynamic offset is enabled (position changes each tick)
    if (resolved != g_lastResolvedText || HasDynamicOffset()) {
        g_lastResolvedText = resolved;
        UpdateOverlays(g_config, resolved.c_str(), g_overlays);
    }
}

static void RecreateOverlays() {
    bool wasVisible = g_watermarkVisible;
    DestroyOverlays(g_overlays);

    g_lastResolvedText = ResolveTemplate(g_config);
    CreateOverlays(g_hInstance, g_config, g_lastResolvedText.c_str(), g_overlays);

    if (!wasVisible) {
        ShowOverlays(g_overlays, false);
    }
}

static void ToggleWatermark() {
    g_watermarkVisible = !g_watermarkVisible;
    if (g_watermarkVisible) {
        // Re-resolve so content is fresh (e.g. {time} updated while hidden)
        g_lastResolvedText = ResolveTemplate(g_config);
        ShowOverlays(g_overlays, true, &g_config, g_lastResolvedText.c_str());
    } else {
        ShowOverlays(g_overlays, false);
    }
}

static UINT ComputeRefreshMs() {
    int base = g_config.refreshInterval;
    if (base <= 0) return 0;
    int extra = 0;
    if (g_config.randomRefreshRange > 0) {
        // RandomRefreshRange is in seconds; convert to ms for the distribution.
        // std::uniform_int_distribution handles arbitrary ranges (unlike rand() which
        // is capped at RAND_MAX=32767, making large ranges silently wrong).
        std::uniform_int_distribution<int> dist(0, g_config.randomRefreshRange * 1000);
        extra = dist(g_rng);
    }
    return (UINT)(base * 1000 + extra);
}

static void SetupRefreshTimer() {
    KillTimer(g_hwndMain, TIMER_REFRESH);
    UINT ms = ComputeRefreshMs();
    if (ms > 0) {
        SetTimer(g_hwndMain, TIMER_REFRESH, ms, nullptr);
    }
}

static void OnConfigChanged() {
    // Reload config
    Config oldConfig = g_config;
    LoadConfig(g_config);

    // Re-register hotkey if changed (always retry in case it previously failed)
    if (oldConfig.hotkeyModifiers != g_config.hotkeyModifiers ||
        oldConfig.hotkeyVk != g_config.hotkeyVk) {
        TryRegisterHotkey();
    }

    // Reset refresh timer
    SetupRefreshTimer();

    // Force re-render (clear cached text to force update)
    g_lastResolvedText.clear();

    // Recreate overlays (handles resolution/monitor changes too)
    RecreateOverlays();
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd, g_watermarkVisible, IsAutoStartEnabled());
            return 0;
        case WM_LBUTTONDBLCLK:
            ToggleWatermark();
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TOGGLE:
            ToggleWatermark();
            return 0;
        case IDM_EDIT_SETTINGS: {
            std::wstring configPath = GetConfigPath();
            ShellExecuteW(nullptr, L"open", configPath.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            StartConfigWatch(hwnd);
            return 0;
        }
        case IDM_AUTOSTART:
            SetAutoStart(!IsAutoStartEnabled());
            return 0;
        case IDM_EXIT:
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE) {
            ToggleWatermark();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_REFRESH) {
            if (g_watermarkVisible) {
                RefreshOverlays();
            }
            // Reschedule with a new random interval if randomRefreshRange > 0
            if (g_config.randomRefreshRange > 0) {
                SetupRefreshTimer();
            }
            return 0;
        }
        if (wParam == TIMER_ZORDER) {
            // Periodic z-order re-assertion — covers apps (e.g. WeChat) that
            // don't trigger EVENT_OBJECT_REORDER or that continuously fight
            // for z-order.  This does NOT re-render content or change positions.
            if (g_watermarkVisible) {
                if (IsAnyMenuVisible()) {
                    // A popup menu is open above the taskbar.
                    // Push our overlay to the top (it's click-through so the menu
                    // stays visible and interactive beneath it), but do NOT call
                    // ReassertTaskbarZOrder() — that would push the taskbar above
                    // the menu and cover it.
                    for (auto& ow : g_overlays)
                        if (ow.hwnd && IsWindow(ow.hwnd) && IsWindowVisible(ow.hwnd))
                            SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                } else {
                    ReassertOverlayZOrder(g_overlays); // taskbar first, overlay above
                }
            }
            return 0;
        }
        if (wParam == TIMER_HOTKEY_NOTIFY) {
            // One-shot: show balloon then cancel
            KillTimer(hwnd, TIMER_HOTKEY_NOTIFY);
            ShowHotkeyErrorBalloon();
            return 0;
        }
        break;

    case WM_CONFIG_CHANGED:
        OnConfigChanged();
        return 0;

    case WM_ZORDER_RECHECK:
        // Another window changed z-order (e.g. WeChat went topmost).
        // Same split logic as TIMER_ZORDER: menu visible → overlay only,
        // no menu → full re-assertion including taskbar.
        if (g_watermarkVisible) {
            if (IsAnyMenuVisible()) {
                for (auto& ow : g_overlays)
                    if (ow.hwnd && IsWindow(ow.hwnd) && IsWindowVisible(ow.hwnd))
                        SetWindowPos(ow.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                ReassertOverlayZOrder(g_overlays);
            }
        }
        return 0;

    case WM_DISPLAYCHANGE:
        // Monitor configuration changed
        RecreateOverlays();
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon(hwnd);
        UnregisterToggleHotkey(hwnd);   // OS also auto-releases on exit
        RemoveZOrderHook();
        StopConfigWatch();
        KillTimer(hwnd, TIMER_REFRESH);
        KillTimer(hwnd, TIMER_ZORDER);
        KillTimer(hwnd, TIMER_HOTKEY_NOTIFY);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single instance check
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SimpleScreenMark_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running
        CloseHandle(hMutex);
        return 0;
    }

    g_hInstance = hInstance;

    // Initialize Winsock (needed for some network functions)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);

    // Load config
    LoadConfig(g_config);

    // Register window classes
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"SimpleScreenMarkMain";
    RegisterClassExW(&wc);

    RegisterOverlayClass(hInstance);

    // Create hidden main window (message-only)
    g_hwndMain = CreateWindowExW(0, L"SimpleScreenMarkMain", L"",
                                  0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!g_hwndMain) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        WSACleanup();
        CloseHandle(hMutex);
        return 1;
    }

    // Create tray icon
    CreateTrayIcon(g_hwndMain, hInstance);

    // Register hotkey — shows balloon if registration fails
    TryRegisterHotkey();

    // Create overlay windows
    RecreateOverlays();

    // Setup refresh timer
    SetupRefreshTimer();

    // Watch for other windows going topmost (e.g. WeChat "always on top")
    // so we can re-assert our overlay z-order reactively.
    InstallZOrderHook();

    // Periodic z-order re-assertion as a fallback for apps that don't trigger
    // EVENT_OBJECT_REORDER (e.g. WeChat).  500ms is lightweight — SetWindowPos
    // with SWP_NOMOVE|SWP_NOSIZE is essentially a no-op when already correct.
    // This timer is independent of TIMER_REFRESH (content/position refresh).
    SetTimer(g_hwndMain, TIMER_ZORDER, 500, nullptr);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    DestroyOverlays(g_overlays);
    StopConfigWatch();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    WSACleanup();
    CloseHandle(hMutex);

    return (int)msg.wParam;
}
