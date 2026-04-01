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

// Shared RNG — mt19937 supports full int range unlike rand() (RAND_MAX=32767)
static std::mt19937 g_rng{std::random_device{}()};

// Forward declarations
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static std::wstring ResolveTemplate(const Config& cfg);
static void RefreshOverlays();
static void RecreateOverlays();
static void ToggleWatermark();
static void TryRegisterHotkey();

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
    ShowOverlays(g_overlays, g_watermarkVisible);
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

    // Update auto-start if changed
    if (oldConfig.autoStart != g_config.autoStart) {
        SetAutoStart(g_config.autoStart);
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
        case IDM_AUTOSTART: {
            bool current = IsAutoStartEnabled();
            SetAutoStart(!current);
            return 0;
        }
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

    case WM_DISPLAYCHANGE:
        // Monitor configuration changed
        RecreateOverlays();
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon(hwnd);
        UnregisterToggleHotkey(hwnd);   // OS also auto-releases on exit
        StopConfigWatch();
        KillTimer(hwnd, TIMER_REFRESH);
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

    // Sync auto-start state
    if (g_config.autoStart) {
        SetAutoStart(true);
    }

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
