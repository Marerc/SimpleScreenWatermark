#pragma once

#include <windows.h>

// Register the global toggle hotkey
bool RegisterToggleHotkey(HWND hwnd, UINT modifiers, UINT vk);

// Unregister the global toggle hotkey
void UnregisterToggleHotkey(HWND hwnd);
