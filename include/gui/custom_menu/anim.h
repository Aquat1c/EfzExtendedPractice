#pragma once

#include "../../utils/config.h"
#include "../../../3rdparty/imgui/imgui.h"

#include <windows.h>

#include <cmath>

namespace CustomMenu::Anim {

constexpr double kDegToRadDivisor = 57.29579143313326;

inline float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// GetTickCount moves in steps of about 16 ms, one game frame, which makes slides judder.
// Never 0: a start tick of 0 means no animation.
inline DWORD NowTick() {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    const DWORD ms = static_cast<DWORD>(counter.QuadPart * 1000 / frequency.QuadPart);
    return ms != 0 ? ms : 1;
}

// 1.0 means finished; the MENU ANIMATIONS setting makes everything finished.
inline float AnimT(DWORD startTick, float durationMs) {
    if (startTick == 0 || durationMs <= 0.0f || !Config::GetSettings().menuAnimations) return 1.0f;
    return Clamp01(static_cast<float>(NowTick() - startTick) / durationMs);
}

// Scales the alpha of every vertex added since firstVertex.
inline void ScaleVertexAlpha(ImDrawList* dl, int firstVertex, float alpha) {
    if (!dl || alpha >= 1.0f) return;
    for (int i = firstVertex; i < dl->VtxBuffer.Size; ++i) {
        ImU32& col = dl->VtxBuffer[i].col;
        const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFFu;
        col = (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(static_cast<float>(a) * alpha) << IM_COL32_A_SHIFT);
    }
}

inline float EfzCosEase(float t01) {
    const double degrees = 180.0 * Clamp01(t01);
    return static_cast<float>((1.0 - std::cos(degrees / kDegToRadDivisor)) * 0.5);
}

} // namespace CustomMenu::Anim
