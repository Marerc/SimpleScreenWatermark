#pragma once

#include <windows.h>

// Register the global toggle hotkey
bool RegisterToggleHotkey(HWND hwnd, UINT modifiers, UINT vk);

// Unregister the global toggle hotkey
void UnregisterToggleHotkey(HWND hwnd);

// Register the temporary hide hotkey
bool RegisterTempHideHotkey(HWND hwnd, UINT modifiers, UINT vk);

// Unregister the temporary hide hotkey
void UnregisterTempHideHotkey(HWND hwnd);