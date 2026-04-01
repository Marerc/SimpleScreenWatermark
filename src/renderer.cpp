#include "renderer.h"
#include "config.h"

// GDI+ headers
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

HBITMAP RenderWatermark(const Config& cfg, int width, int height,
                        const wchar_t* resolvedText,
                        float offsetX, float offsetY) {
    // Create 32bpp ARGB bitmap
    Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);

    // Clear to fully transparent
    graphics.Clear(Color(0, 0, 0, 0));

    // Set text rendering hint
    if (cfg.aliased) {
        graphics.SetTextRenderingHint(TextRenderingHintSingleBitPerPixel);
    } else {
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    }

    // Resolve font name: GDI+ uses the system-locale name.
    // On English Windows, Chinese fonts are registered by their English names
    // (e.g. SimSun instead of the Simplified Chinese name).
    // All Chinese strings below are written as \uXXXX escapes to keep the
    // source file ASCII-clean and avoid MSVC code-page issues.
    //
    // \u5B8B\u4F53 = 宋体   \u9ED1\u4F53 = 黑体   \u6977\u4F53 = 楷体
    // \u4EFF\u5B8B = 仿宋   \u5FAE\u8F6F\u96C5\u9ED1 = 微软雅黑
    // \u5FAE\u8EDF\u6B63\u9ED1\u9AD4 = 微軟正黑體  \u65B0\u7EC6\u660E\u4F53 = 新细明体
    struct FontAlias { const wchar_t* name1; const wchar_t* name2; };
    static const FontAlias kAliases[] = {
        { L"\u5B8B\u4F53",                     L"SimSun"              },
        { L"\u9ED1\u4F53",                     L"SimHei"              },
        { L"\u6977\u4F53",                     L"KaiTi"               },
        { L"\u4EFF\u5B8B",                     L"FangSong"            },
        { L"\u5FAE\u8F6F\u96C5\u9ED1",         L"Microsoft YaHei"     },
        { L"\u5FAE\u8EDF\u6B63\u9ED1\u9AD4",   L"Microsoft JhengHei"  },
        { L"\u65B0\u7EC6\u660E\u4F53",         L"PMingLiU"            },
    };

    std::wstring resolvedFontName = cfg.fontName;
    {
        FontFamily probe(cfg.fontName.c_str());
        if (!probe.IsAvailable()) {
            // Try the alternate name (name1 <-> name2 are the two locale names)
            for (const auto& alias : kAliases) {
                if (cfg.fontName == alias.name1) {
                    FontFamily alt(alias.name2);
                    if (alt.IsAvailable()) { resolvedFontName = alias.name2; break; }
                }
                if (cfg.fontName == alias.name2) {
                    FontFamily alt(alias.name1);
                    if (alt.IsAvailable()) { resolvedFontName = alias.name1; break; }
                }
            }
            // Ultimate fallback
            FontFamily check(resolvedFontName.c_str());
            if (!check.IsAvailable()) resolvedFontName = L"Arial";
        }
    }

    Font font(resolvedFontName.c_str(), (REAL)cfg.fontSize, FontStyleRegular, UnitPixel);

    BYTE r = GetRValue(cfg.fontColor);
    BYTE g = GetGValue(cfg.fontColor);
    BYTE b = GetBValue(cfg.fontColor);
    SolidBrush brush(Color((BYTE)cfg.opacity, r, g, b));

    // Measure text size for grid spacing
    RectF textBounds;
    graphics.MeasureString(resolvedText, -1, &font, PointF(0, 0), &textBounds);

    float textW = textBounds.Width;
    float textH = textBounds.Height;

    // Grid spacing (at least text size + some gap)
    float spacingX = (float)cfg.spacingX;
    float spacingY = (float)cfg.spacingY;
    if (spacingX < textW + 20) spacingX = textW + 20;
    if (spacingY < textH + 10) spacingY = textH + 10;

    // Calculate expanded bounds for rotation
    // We need to cover the visible rect after rotation
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;
    float diag = sqrtf((float)(width * width + height * height));

    float expandedW = diag;
    float expandedH = diag;

    // Apply rotation transform around center of visible area
    graphics.TranslateTransform(centerX, centerY);
    graphics.RotateTransform(cfg.angle);
    graphics.TranslateTransform(-centerX, -centerY);

    // Calculate grid origin (expanded area centered on visible area)
    float gridOriginX = centerX - expandedW / 2 + offsetX;
    float gridOriginY = centerY - expandedH / 2 + offsetY;

    // Draw text grid — columns strictly aligned, no staggering
    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);

    for (float y = gridOriginY; y < gridOriginY + expandedH; y += spacingY) {
        for (float x = gridOriginX; x < gridOriginX + expandedW; x += spacingX) {
            PointF pt(x, y);
            graphics.DrawString(resolvedText, -1, &font, pt, &format, &brush);
        }
    }

    // Convert to HBITMAP
    HBITMAP hBitmap = nullptr;
    bitmap.GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
    return hBitmap;
}
