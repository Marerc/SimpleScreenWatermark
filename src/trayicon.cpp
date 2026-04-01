#include "trayicon.h"
#include "resource.h"
#include <shellapi.h>

void CreateTrayIcon(HWND hwnd, HINSTANCE hInstance) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"SimpleScreenMark");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Set version for modern behavior
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowBalloonTip(HWND hwnd, const wchar_t* title, const wchar_t* msg,
                    DWORD infoFlags) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize  = sizeof(nid);
    nid.hWnd    = hwnd;
    nid.uID     = 1;
    nid.uFlags  = NIF_INFO;
    nid.dwInfoFlags = infoFlags;
    wcscpy_s(nid.szInfoTitle, title);
    wcscpy_s(nid.szInfo,      msg);
    nid.uTimeout = 8000;   // 8 seconds (ignored on Vista+ but required)
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowTrayMenu(HWND hwnd, bool watermarkVisible, bool autoStartEnabled) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING | (watermarkVisible ? MF_CHECKED : 0),
                IDM_TOGGLE, L"Show Watermark");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EDIT_SETTINGS, L"Edit Settings...");
    AppendMenuW(hMenu, MF_STRING | (autoStartEnabled ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"Start with Windows");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    // Required for menu to dismiss properly
    SetForegroundWindow(hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);

    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}
