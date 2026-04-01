#pragma once

#include <windows.h>
#include <string>

struct Config {
    // [Watermark]
    std::wstring templateText = L"{hostname} {ip} {time}";
    std::wstring fontName     = L"Microsoft YaHei";
    int          fontSize     = 16;
    COLORREF     fontColor    = RGB(0x88, 0x88, 0x88);
    int          opacity      = 30;   // 0-255
    int          spacingX     = 300;
    int          spacingY     = 200;
    float        angle        = -25.0f;
    bool         aliased      = false;

    // [Time]
    std::wstring timeFormat      = L"%Y-%m-%d";
    int          refreshInterval = 1;  // seconds, 0 = no refresh
    int          randomRefreshRange = 0; // random extra seconds added to interval

    // [Dynamic]
    int          randomOffsetX  = 0;   // random X shift range in pixels
    int          randomOffsetY  = 0;   // random Y shift range in pixels

    // [Network]
    std::wstring nic = L"auto";

    // [Hotkey]
    UINT hotkeyModifiers = MOD_CONTROL;
    UINT hotkeyVk        = 'W';

    // [General]
    bool autoStart = false;
};

// Get config file path (%APPDATA%\SimpleScreenMark\config.ini)
std::wstring GetConfigPath();

// Get config directory path
std::wstring GetConfigDir();

// Load config from INI file. Creates default if not exists.
void LoadConfig(Config& cfg);

// Write default config file
void WriteDefaultConfig(const std::wstring& path);

// Parse hotkey string like "Ctrl+W" into modifiers and vk
void ParseHotkeyString(const std::wstring& str, UINT& modifiers, UINT& vk);

// Start watching config file for changes (call from edit settings)
void StartConfigWatch(HWND hwndNotify);

// Stop watching config file
void StopConfigWatch();
