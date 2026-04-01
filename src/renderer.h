#pragma once

#include <windows.h>

struct Config;

// Render watermark bitmap for a given screen area.
// Returns a 32bpp ARGB HBITMAP. Caller must DeleteObject.
// width/height are the overlay window dimensions.
// offsetX/offsetY are random pixel offsets applied to the grid origin.
HBITMAP RenderWatermark(const Config& cfg, int width, int height,
                        const wchar_t* resolvedText,
                        float offsetX = 0.0f, float offsetY = 0.0f);
