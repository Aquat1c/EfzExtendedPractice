#pragma once
#include <string>

struct ImDrawList;
struct ImFont;

namespace Mission::Render {

struct RichTextMetrics {
    float width = 0.0f;
    float height = 0.0f;
};

// ox/oy/scale map the 640x480 virtual space into the render target's inner 4:3 area.
// WantsDraw is a lock-free render-reason hint; Draw performs the coherent gate.
bool WantsDraw();
void Draw(void* device, ImDrawList* dl, float ox, float oy, float scale);

float MeasureRichTextHeight(void* device, ImFont* font, float fontPx,
                            const std::string& text, float maxWidth);
RichTextMetrics MeasureRichText(void* device, ImFont* font, float fontPx,
                                const std::string& text, float maxWidth);
void DrawRichText(void* device, ImDrawList* dl, ImFont* font, float fontPx,
                  float x, float y, unsigned int color,
                  const std::string& text, float maxWidth);

void InvalidateTextLayouts();

void ReleaseTextures();

}
