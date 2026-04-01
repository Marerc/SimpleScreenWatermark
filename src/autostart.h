#pragma once

#include <windows.h>

// Check if auto-start registry entry exists and points to current exe
bool IsAutoStartEnabled();

// Set or remove auto-start registry entry
void SetAutoStart(bool enable);
