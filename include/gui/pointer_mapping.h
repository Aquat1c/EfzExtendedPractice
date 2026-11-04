#pragma once

#include "imgui_internal.h"
#include <cstdint>
#include <cstring>

namespace GuiPointer {

enum class ScaleMode { Stretch, Fit, Integer, Custom };
struct Settings {
    ScaleMode mode = ScaleMode::Stretch;
    int integerScale = 0;
    int customWidth = 640;
    int customHeight = 480;
};
struct Rect { int x = 0, y = 0, w = 0, h = 0; };

inline Rect ImageRect(int width, int height, const Settings& settings) {
    if (width <= 0 || height <= 0) return {};
    int w = width, h = height;
    if (settings.mode == ScaleMode::Fit) {
        if (int64_t(width) * 480 <= int64_t(height) * 640)
            h = int((int64_t(960) * width + 640) / 1280);
        else
            w = int((int64_t(1280) * height + 480) / 960);
    } else if (settings.mode == ScaleMode::Integer) {
        int fit = (width / 640 < height / 480) ? width / 640 : height / 480;
        if (fit < 1) fit = 1;
        int scale = settings.integerScale > 0 ? settings.integerScale : fit;
        if (scale > fit) scale = fit;
        w = 640 * scale;
        h = 480 * scale;
    } else if (settings.mode == ScaleMode::Custom) {
        w = settings.customWidth > 0 ? settings.customWidth : 640;
        h = settings.customHeight > 0 ? settings.customHeight : 480;
    }
    w = (w > width) ? width : ((w < 1) ? 1 : w);
    h = (h > height) ? height : ((h < 1) ? 1 : h);
    return {(width - w) / 2, (height - h) / 2, w, h};
}

inline ImVec2 Map(float x, float y, int clientWidth, int clientHeight,
                  int backbufferWidth, int backbufferHeight, const Settings& settings) {
    if (clientWidth <= 0 || clientHeight <= 0) return ImVec2(-FLT_MAX, -FLT_MAX);
    if (backbufferWidth <= 0 || backbufferHeight <= 0) {
        backbufferWidth = clientWidth;
        backbufferHeight = clientHeight;
    }
    const Rect r = ImageRect(backbufferWidth, backbufferHeight, settings);
    const float bx = x * backbufferWidth / clientWidth;
    const float by = y * backbufferHeight / clientHeight;
    if (bx < r.x || by < r.y || bx >= r.x + r.w || by >= r.y + r.h)
        return ImVec2(-FLT_MAX, -FLT_MAX);
    return ImVec2((bx - r.x) * 640.0f / r.w, (by - r.y) * 480.0f / r.h);
}

inline ImVec2 Unmap(float x, float y, int clientWidth, int clientHeight,
                    int backbufferWidth, int backbufferHeight, const Settings& settings) {
    if (clientWidth <= 0 || clientHeight <= 0) return ImVec2(-FLT_MAX, -FLT_MAX);
    if (backbufferWidth <= 0 || backbufferHeight <= 0) {
        backbufferWidth = clientWidth;
        backbufferHeight = clientHeight;
    }
    const Rect r = ImageRect(backbufferWidth, backbufferHeight, settings);
    return ImVec2((r.x + x * r.w / 640.0f) * clientWidth / backbufferWidth,
                  (r.y + y * r.h / 480.0f) * clientHeight / backbufferHeight);
}

inline uint32_t FinalBlitRva(uint32_t timestamp) {
    switch (timestamp) {
    case 0x5EA72E7Fu: case 0x5F8C5897u: case 0x623F8621u: case 0x625C3202u: return 0x1FC1Fu;
    case 0x63BF27C3u: return 0x1FCCFu;
    case 0x6A04FEC7u: return 0x27900u;
    case 0x6A6F1795u: return 0x279C0u;
    default: return 0;
    }
}

inline bool OwnsFinalBlit(const unsigned char* bytes, unsigned byteCount,
                         uint32_t site, uint32_t ownerBase, uint32_t ownerSize) {
    if (byteCount < 6 || bytes[0] != 0xE8 || bytes[5] != 0x90 || ownerSize == 0) return false;
    int32_t relative = 0;
    std::memcpy(&relative, bytes + 1, sizeof(relative));
    const uint32_t target = site + 5u + uint32_t(relative);
    return target >= ownerBase && target - ownerBase < ownerSize;
}

inline void DiscardBackendMousePositions(ImGuiContext& context, int firstEvent) {
    for (int i = firstEvent; i < context.InputEventsQueue.Size;) {
        if (context.InputEventsQueue[i].Type == ImGuiInputEventType_MousePos)
            context.InputEventsQueue.erase(context.InputEventsQueue.Data + i);
        else
            ++i;
    }
}

inline void ReplaceBackendMousePosition(ImGuiContext& context, int firstEvent, const ImVec2& pos) {
    DiscardBackendMousePositions(context, firstEvent);
    context.IO.AddMousePosEvent(pos.x, pos.y);
}

}
