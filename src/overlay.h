#pragma once

#include <windows.h>
#include <vector>

struct Config;

struct OverlayWindow {
    HWND hwnd = nullptr;
    RECT workArea = {};
};

// Create overlay windows for all monitors
void CreateOverlays(HINSTANCE hInstance, const Config& cfg,
                    const wchar_t* resolvedText,
                    std::vector<OverlayWindow>& overlays);

// Update overlay contents (re-render watermark)
void UpdateOverlays(const Config& cfg, const wchar_t* resolvedText,
                    std::vector<OverlayWindow>& overlays);

// Show or hide all overlays
void ShowOverlays(std::vector<OverlayWindow>& overlays, bool show);

// Destroy all overlay windows
void DestroyOverlays(std::vector<OverlayWindow>& overlays);

// Register the overlay window class
void RegisterOverlayClass(HINSTANCE hInstance);
