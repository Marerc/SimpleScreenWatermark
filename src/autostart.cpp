#include "autostart.h"
#include <string>

static const wchar_t* REG_RUN_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* REG_VALUE_NAME = L"SimpleScreenMark";

static std::wstring GetExePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH];
    DWORD size = sizeof(value);
    DWORD type;
    bool enabled = false;

    if (RegQueryValueExW(hKey, REG_VALUE_NAME, nullptr, &type,
                         (LPBYTE)value, &size) == ERROR_SUCCESS) {
        if (type == REG_SZ) {
            std::wstring exePath = GetExePath();
            enabled = (exePath == value);
        }
    }

    RegCloseKey(hKey);
    return enabled;
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return;
    }

    if (enable) {
        std::wstring exePath = GetExePath();
        RegSetValueExW(hKey, REG_VALUE_NAME, 0, REG_SZ,
                       (const BYTE*)exePath.c_str(),
                       (DWORD)((exePath.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, REG_VALUE_NAME);
    }

    RegCloseKey(hKey);
}
