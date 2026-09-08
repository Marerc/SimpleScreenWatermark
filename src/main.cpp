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
static bool         g_tempHideActive  = false;  // true when temporarily hidden by hotkey
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
static void TempHideWatermark();
static void RestoreWatermark();
static void TryRegisterHotkey();
static void InstallZOrderHook();
static void RemoveZOrderHook();
static void SimulateHotkey(UINT modifiers, UINT vk);

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

// Simulate a hotkey press using SendInput
static void SimulateHotkey(UINT modifiers, UINT vk) {
    std::vector<INPUT> inputs;
    
    // Build key down events for modifiers
    if (modifiers & MOD_CONTROL) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_CONTROL;
        inputs.push_back(input);
    }
    if (modifiers & MOD_ALT) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_MENU;
        inputs.push_back(input);
    }
    if (modifiers & MOD_SHIFT) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_SHIFT;
        inputs.push_back(input);
    }
    if (modifiers & MOD_WIN) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_LWIN;
        inputs.push_back(input);
    }
    
    // Key down for the main key
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = (WORD)vk;
        inputs.push_back(input);
    }
    
    // Key up for the main key (in reverse order)
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = (WORD)vk;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    
    // Key up for modifiers (in reverse order)
    if (modifiers & MOD_WIN) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_LWIN;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    if (modifiers & MOD_SHIFT) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_SHIFT;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    if (modifiers & MOD_ALT) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_MENU;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    if (modifiers & MOD_CONTROL) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_CONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    
    // Send all inputs
    SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
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

    // Register temporary hide hotkey
    UnregisterTempHideHotkey(g_hwndMain);
    RegisterTempHideHotkey(g_hwndMain, g_config.tempHideModifiers, g_config.tempHideVk);
}

// Returns true if any popup menu is currently visible above the taskbar.
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
        if (!(exStyle & WS_EX_TOPMOST)) break;

        bool isOurs = false;
        for (const auto& ow : g_overlays)
            if (ow.hwnd == w) { isOurs = true; break; }
        if (isOurs) continue;

        wchar_t cls[64] = {};
        GetClassNameW(w, cls, 64);
        if (wcscmp(cls, L"Shell_TrayWnd") == 0 ||
            wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) continue;

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

// WinEvent callback
static void CALLBACK ZOrderEventProc(HWINEVENTHOOK, DWORD event,
                                      HWND hwndChanged, LONG idObject,
                                      LONG, DWORD, DWORD) {
    if (idObject != OBJID_WINDOW) return;

    for (const auto& ow : g_overlays) {
        if (ow.hwnd == hwndChanged) return;
    }

    if (!g_watermarkVisible) return;

    PostMessage(g_hwndMain, WM_APP + 3, 0, 0);
}

static void InstallZOrderHook() {
    if (g_zorderHook) return;
    g_zorderHook = SetWinEventHook(
        EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER,
        nullptr, ZOrderEventProc,
        0, 0,
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

    size_t pos;
    while ((pos = text.find(L"{hostname}")) != std::wstring::npos) {
        text.replace(pos, 10, GetHostname());
    }

    while ((pos = text.find(L"{ip}")) != std::wstring::npos) {
        text.replace(pos, 4, GetIPAddress(cfg.nic));
    }

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
    if (g_tempHideActive) {
        KillTimer(g_hwndMain, TIMER_TEMP_HIDE);
        KillTimer(g_hwndMain, TIMER_SIMULATE_SCREENSHOT);
        g_tempHideActive = false;
    }

    g_watermarkVisible = !g_watermarkVisible;
    if (g_watermarkVisible) {
        g_lastResolvedText = ResolveTemplate(g_config);
        ShowOverlays(g_overlays, true, &g_config, g_lastResolvedText.c_str());
    } else {
        ShowOverlays(g_overlays, false);
    }
}

// Temporarily hide the watermark for a configured duration, then auto-restore
static void TempHideWatermark() {
    // If already temporarily hidden, do nothing
    if (g_tempHideActive) return;

    g_tempHideActive = true;
    g_watermarkVisible = false;
    ShowOverlays(g_overlays, false);

    // Start timer to auto-restore watermark
    UINT durationMs = (UINT)(g_config.tempHideDuration * 1000);
    SetTimer(g_hwndMain, TIMER_TEMP_HIDE, durationMs, nullptr);
    
    // Use timer to simulate screenshot hotkey after 150ms delay
    // This ensures the overlay windows are fully hidden before sending the hotkey
    SetTimer(g_hwndMain, TIMER_SIMULATE_SCREENSHOT, 150, nullptr);
}

// Restore watermark after temporary hide
static void RestoreWatermark() {
    g_tempHideActive = false;
    g_watermarkVisible = true;

    g_lastResolvedText = ResolveTemplate(g_config);
    ShowOverlays(g_overlays, true, &g_config, g_lastResolvedText.c_str());
}

static UINT ComputeRefreshMs() {
    int base = g_config.refreshInterval;
    if (base <= 0) return 0;
    int extra = 0;
    if (g_config.randomRefreshRange > 0) {
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
    Config oldConfig = g_config;
    LoadConfig(g_config);

    if (oldConfig.hotkeyModifiers != g_config.hotkeyModifiers ||
        oldConfig.hotkeyVk != g_config.hotkeyVk) {
        TryRegisterHotkey();
    }

    if (oldConfig.tempHideModifiers != g_config.tempHideModifiers ||
        oldConfig.tempHideVk != g_config.tempHideVk) {
        UnregisterTempHideHotkey(g_hwndMain);
        RegisterTempHideHotkey(g_hwndMain, g_config.tempHideModifiers, g_config.tempHideVk);
    }

    SetupRefreshTimer();

    g_lastResolvedText.clear();

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
        if (wParam == HOTKEY_TEMP_HIDE) {
            TempHideWatermark();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_REFRESH) {
            if (g_watermarkVisible) {
                RefreshOverlays();
            }
            if (g_config.randomRefreshRange > 0) {
                SetupRefreshTimer();
            }
            return 0;
        }
        if (wParam == TIMER_ZORDER) {
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
        }
        if (wParam == TIMER_HOTKEY_NOTIFY) {
            KillTimer(hwnd, TIMER_HOTKEY_NOTIFY);
            ShowHotkeyErrorBalloon();
            return 0;
        }
        if (wParam == TIMER_TEMP_HIDE) {
            // Auto-restore watermark after temporary hide
            KillTimer(hwnd, TIMER_TEMP_HIDE);
            RestoreWatermark();
            return 0;
        }
        if (wParam == TIMER_SIMULATE_SCREENSHOT) {
            // One-shot: simulate screenshot hotkey
            KillTimer(hwnd, TIMER_SIMULATE_SCREENSHOT);
            SimulateHotkey(g_config.screenshotModifiers, g_config.screenshotVk);
            return 0;
        }
        break;

    case WM_CONFIG_CHANGED:
        OnConfigChanged();
        return 0;

    case WM_ZORDER_RECHECK:
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
        RecreateOverlays();
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon(hwnd);
        UnregisterToggleHotkey(hwnd);
        UnregisterTempHideHotkey(hwnd);
        RemoveZOrderHook();
        StopConfigWatch();
        KillTimer(hwnd, TIMER_REFRESH);
        KillTimer(hwnd, TIMER_ZORDER);
        KillTimer(hwnd, TIMER_HOTKEY_NOTIFY);
        KillTimer(hwnd, TIMER_TEMP_HIDE);
        KillTimer(hwnd, TIMER_SIMULATE_SCREENSHOT);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SimpleScreenMark_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    g_hInstance = hInstance;

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);

    LoadConfig(g_config);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"SimpleScreenMarkMain";
    RegisterClassExW(&wc);

    RegisterOverlayClass(hInstance);

    g_hwndMain = CreateWindowExW(0, L"SimpleScreenMarkMain", L"",
                                  0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!g_hwndMain) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        WSACleanup();
        CloseHandle(hMutex);
        return 1;
    }

    CreateTrayIcon(g_hwndMain, hInstance);

    TryRegisterHotkey();

    RecreateOverlays();

    SetupRefreshTimer();

    InstallZOrderHook();

    SetTimer(g_hwndMain, TIMER_ZORDER, 500, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyOverlays(g_overlays);
    StopConfigWatch();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    WSACleanup();
    CloseHandle(hMutex);

    return (int)msg.wParam;
}