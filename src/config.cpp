#include "config.h"
#include "resource.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <cctype>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

#pragma comment(lib, "shlwapi.lib")

static std::atomic<bool> g_watching{false};
static HANDLE g_watchThread = nullptr;

std::wstring GetConfigDir() {
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData);
    std::wstring dir = std::wstring(appData) + L"\\SimpleScreenMark";
    return dir;
}

std::wstring GetConfigPath() {
    return GetConfigDir() + L"\\config.ini";
}

static std::wstring ReadIniString(const wchar_t* section, const wchar_t* key,
                                   const wchar_t* def, const wchar_t* path) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(section, key, def, buf, _countof(buf), path);
    return buf;
}

static int ReadIniInt(const wchar_t* section, const wchar_t* key,
                      int def, const wchar_t* path) {
    return GetPrivateProfileIntW(section, key, def, path);
}

static COLORREF ParseHexColor(const std::wstring& hex) {
    unsigned int val = 0;
    swscanf_s(hex.c_str(), L"%x", &val);
    BYTE r = (val >> 16) & 0xFF;
    BYTE g = (val >> 8) & 0xFF;
    BYTE b = val & 0xFF;
    return RGB(r, g, b);
}

static std::wstring ToUpper(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}

void ParseHotkeyString(const std::wstring& str, UINT& modifiers, UINT& vk) {
    modifiers = 0;
    vk = 0;

    // Split by '+'
    std::wstring s = str;
    std::vector<std::wstring> parts;
    size_t pos;
    while ((pos = s.find(L'+')) != std::wstring::npos) {
        std::wstring part = s.substr(0, pos);
        // Trim whitespace
        while (!part.empty() && part.front() == L' ') part.erase(part.begin());
        while (!part.empty() && part.back() == L' ') part.pop_back();
        if (!part.empty()) parts.push_back(part);
        s = s.substr(pos + 1);
    }
    // Trim last part
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    while (!s.empty() && s.back() == L' ') s.pop_back();
    if (!s.empty()) parts.push_back(s);

    for (const auto& part : parts) {
        std::wstring upper = ToUpper(part);
        if (upper == L"CTRL" || upper == L"CONTROL") {
            modifiers |= MOD_CONTROL;
        } else if (upper == L"ALT") {
            modifiers |= MOD_ALT;
        } else if (upper == L"SHIFT") {
            modifiers |= MOD_SHIFT;
        } else if (upper == L"WIN") {
            modifiers |= MOD_WIN;
        } else if (upper.size() == 1 && upper[0] >= L'A' && upper[0] <= L'Z') {
            vk = upper[0];
        } else if (upper.size() >= 2 && upper[0] == L'F') {
            int fnum = _wtoi(upper.c_str() + 1);
            if (fnum >= 1 && fnum <= 24) {
                vk = VK_F1 + (fnum - 1);
            }
        } else if (upper == L"SPACE") {
            vk = VK_SPACE;
        } else if (upper == L"TAB") {
            vk = VK_TAB;
        } else if (upper == L"ESCAPE" || upper == L"ESC") {
            vk = VK_ESCAPE;
        }
    }

    // Fallback
    if (vk == 0) {
        modifiers = MOD_CONTROL;
        vk = 'W';
    }
}

void WriteDefaultConfig(const std::wstring& path) {
    // Ensure directory exists
    std::wstring dir = GetConfigDir();
    CreateDirectoryW(dir.c_str(), nullptr);

    // Pre-create the file with a UTF-16 LE BOM so that WritePrivateProfileStringW
    // operates in Unicode mode on all systems (including English code-page 1252).
    // Without the BOM the API writes ANSI, which corrupts non-ASCII chars like "宋体".
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        WORD bom = 0xFEFF;
        DWORD written = 0;
        WriteFile(hFile, &bom, sizeof(bom), &written, nullptr);
        CloseHandle(hFile);
    }

    const wchar_t* p = path.c_str();

    WritePrivateProfileStringW(L"Watermark", L"Template", L"{hostname} {ip} {time}", p);
    // L"\u5B8B\u4F53" = 宋体  (Unicode escape keeps source file ASCII-clean)
    WritePrivateProfileStringW(L"Watermark", L"FontName", L"\u5B8B\u4F53", p);
    WritePrivateProfileStringW(L"Watermark", L"FontSize", L"20", p);
    WritePrivateProfileStringW(L"Watermark", L"FontColor", L"808080", p);
    WritePrivateProfileStringW(L"Watermark", L"Opacity", L"50", p);
    WritePrivateProfileStringW(L"Watermark", L"SpacingX", L"700", p);
    WritePrivateProfileStringW(L"Watermark", L"SpacingY", L"400", p);
    WritePrivateProfileStringW(L"Watermark", L"Angle", L"-42", p);
    WritePrivateProfileStringW(L"Watermark", L"Aliased", L"1", p);

    WritePrivateProfileStringW(L"Time", L"Format", L"%Y-%m-%d", p);
    WritePrivateProfileStringW(L"Time", L"RefreshInterval", L"1", p);
    WritePrivateProfileStringW(L"Time", L"RandomRefreshRange", L"0", p);

    WritePrivateProfileStringW(L"Dynamic", L"RandomOffsetXRange", L"0", p);
    WritePrivateProfileStringW(L"Dynamic", L"RandomOffsetYRange", L"0", p);

    WritePrivateProfileStringW(L"Network", L"NIC", L"auto", p);

    WritePrivateProfileStringW(L"Hotkey", L"Hotkey", L"Ctrl+W", p);

    WritePrivateProfileStringW(L"General", L"AutoStart", L"0", p);
}

void LoadConfig(Config& cfg) {
    std::wstring path = GetConfigPath();

    // Create default config if not exists
    if (!PathFileExistsW(path.c_str())) {
        WriteDefaultConfig(path);
    }

    const wchar_t* p = path.c_str();

    cfg.templateText    = ReadIniString(L"Watermark", L"Template", L"{hostname} {ip} {time}", p);
    cfg.fontName        = ReadIniString(L"Watermark", L"FontName", L"Microsoft YaHei", p);
    cfg.fontSize        = ReadIniInt(L"Watermark", L"FontSize", 16, p);
    cfg.fontColor       = ParseHexColor(ReadIniString(L"Watermark", L"FontColor", L"888888", p));
    cfg.opacity         = ReadIniInt(L"Watermark", L"Opacity", 30, p);
    cfg.spacingX        = ReadIniInt(L"Watermark", L"SpacingX", 300, p);
    cfg.spacingY        = ReadIniInt(L"Watermark", L"SpacingY", 200, p);
    cfg.aliased         = ReadIniInt(L"Watermark", L"Aliased", 0, p) != 0;

    // Parse angle as float from string
    std::wstring angleStr = ReadIniString(L"Watermark", L"Angle", L"-25", p);
    cfg.angle = (float)_wtof(angleStr.c_str());

    cfg.timeFormat         = ReadIniString(L"Time", L"Format", L"%Y-%m-%d", p);
    cfg.refreshInterval    = ReadIniInt(L"Time", L"RefreshInterval", 1, p);
    cfg.randomRefreshRange = ReadIniInt(L"Time", L"RandomRefreshRange", 0, p);

    cfg.randomOffsetX  = ReadIniInt(L"Dynamic", L"RandomOffsetXRange", 0, p);
    cfg.randomOffsetY  = ReadIniInt(L"Dynamic", L"RandomOffsetYRange", 0, p);

    cfg.nic             = ReadIniString(L"Network", L"NIC", L"auto", p);

    std::wstring hotkeyStr = ReadIniString(L"Hotkey", L"Hotkey", L"Ctrl+W", p);
    ParseHotkeyString(hotkeyStr, cfg.hotkeyModifiers, cfg.hotkeyVk);

    cfg.autoStart       = ReadIniInt(L"General", L"AutoStart", 0, p) != 0;

    // Clamp values
    if (cfg.opacity < 0) cfg.opacity = 0;
    if (cfg.opacity > 255) cfg.opacity = 255;
    if (cfg.fontSize < 6) cfg.fontSize = 6;
    if (cfg.fontSize > 200) cfg.fontSize = 200;
    if (cfg.spacingX < 50) cfg.spacingX = 50;
    if (cfg.spacingY < 50) cfg.spacingY = 50;
    if (cfg.randomRefreshRange < 0) cfg.randomRefreshRange = 0;
    if (cfg.randomOffsetX < 0) cfg.randomOffsetX = 0;
    if (cfg.randomOffsetY < 0) cfg.randomOffsetY = 0;
}

// File watcher thread
static DWORD WINAPI ConfigWatchThread(LPVOID param) {
    HWND hwndNotify = (HWND)param;
    std::wstring dir = GetConfigDir();

    HANDLE hChange = FindFirstChangeNotificationW(
        dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);

    if (hChange == INVALID_HANDLE_VALUE) return 1;

    while (g_watching.load()) {
        DWORD result = WaitForSingleObject(hChange, 500);
        if (!g_watching.load()) break;

        if (result == WAIT_OBJECT_0) {
            // Debounce: wait a bit for editor to finish writing
            Sleep(300);
            PostMessage(hwndNotify, WM_CONFIG_CHANGED, 0, 0);
            // Continue watching for more changes during edit session
            FindNextChangeNotification(hChange);
        }
    }

    FindCloseChangeNotification(hChange);
    return 0;
}

void StartConfigWatch(HWND hwndNotify) {
    if (g_watching.load()) return;  // Already watching
    g_watching.store(true);
    g_watchThread = CreateThread(nullptr, 0, ConfigWatchThread,
                                 (LPVOID)hwndNotify, 0, nullptr);
}

void StopConfigWatch() {
    g_watching.store(false);
    if (g_watchThread) {
        WaitForSingleObject(g_watchThread, 2000);
        CloseHandle(g_watchThread);
        g_watchThread = nullptr;
    }
}
