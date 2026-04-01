#pragma once

#include <windows.h>
#include <shellapi.h>

// Create the tray icon
void CreateTrayIcon(HWND hwnd, HINSTANCE hInstance);

// Remove the tray icon
void RemoveTrayIcon(HWND hwnd);

// Show the tray context menu
void ShowTrayMenu(HWND hwnd, bool watermarkVisible, bool autoStartEnabled);

// Show a balloon tip notification from the tray icon
// infoFlags: NIIF_INFO / NIIF_WARNING / NIIF_ERROR
void ShowBalloonTip(HWND hwnd, const wchar_t* title, const wchar_t* msg,
                    DWORD infoFlags = NIIF_WARNING);
