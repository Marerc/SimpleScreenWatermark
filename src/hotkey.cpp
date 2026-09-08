#include "hotkey.h"
#include "resource.h"

bool RegisterToggleHotkey(HWND hwnd, UINT modifiers, UINT vk) {
    return RegisterHotKey(hwnd, HOTKEY_TOGGLE, modifiers | MOD_NOREPEAT, vk) != 0;
}

void UnregisterToggleHotkey(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
}

bool RegisterTempHideHotkey(HWND hwnd, UINT modifiers, UINT vk) {
    return RegisterHotKey(hwnd, HOTKEY_TEMP_HIDE, modifiers | MOD_NOREPEAT, vk) != 0;
}

void UnregisterTempHideHotkey(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_TEMP_HIDE);
}