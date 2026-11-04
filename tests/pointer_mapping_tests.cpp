#include "gui/pointer_mapping.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
static void check(bool value, const char* text) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", text); ++failures; }
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    using namespace GuiPointer;
    Settings settings;
    settings.mode = ScaleMode::Fit;
    Rect rect = ImageRect(2560, 1377, settings);
    check(rect.x == 362 && rect.y == 0 && rect.w == 1836 && rect.h == 1377, "observed FitToScreen rectangle");
    ImVec2 p = Map(362, 0, 2560, 1377, 2560, 1377, settings);
    check(near(p.x, 0) && near(p.y, 0), "letterbox origin");
    p = Map(1280, 688.5f, 2560, 1377, 2560, 1377, settings);
    check(near(p.x, 320) && near(p.y, 240), "letterbox center");
    check(Map(361, 100, 2560, 1377, 2560, 1377, settings).x == -FLT_MAX, "border does not become canvas edge");
    check(Map(2198, 100, 2560, 1377, 2560, 1377, settings).x == -FLT_MAX, "right edge is outside");
    p = Map(181, 0, 1280, 688, 2560, 1377, settings);
    check(near(p.x, 0), "backbuffer size differs from client");
    rect = ImageRect(800, 1000, settings);
    check(rect.x == 0 && rect.y == 200 && rect.w == 800 && rect.h == 600, "portrait letterbox");
    rect = ImageRect(1001, 700, settings);
    check(rect.w == 933 && rect.x == 34, "integer-rounded Fit dimensions");
    settings.mode = ScaleMode::Integer;
    rect = ImageRect(2560, 1377, settings);
    check(rect.w == 1280 && rect.h == 960 && rect.x == 640 && rect.y == 208, "automatic integer scale");
    settings.integerScale = 1;
    rect = ImageRect(2560, 1377, settings);
    check(rect.w == 640 && rect.h == 480 && rect.x == 960, "explicit integer scale");
    settings.integerScale = 99;
    check(ImageRect(2560, 1377, settings).w == 1280, "integer scale clamps to fit");
    rect = ImageRect(320, 240, settings);
    check(rect.w == 320 && rect.h == 240, "small output clips integer scale");
    settings.mode = ScaleMode::Custom;
    settings.customWidth = 900; settings.customHeight = 500;
    rect = ImageRect(1000, 700, settings);
    check(rect.x == 50 && rect.y == 100 && rect.w == 900 && rect.h == 500, "custom centered extent");
    settings.customWidth = 2000;
    check(ImageRect(1000, 700, settings).w == 1000, "custom clamps to backbuffer");
    settings.mode = ScaleMode::Stretch;
    p = Map(640, 360, 1280, 720, 640, 480, settings);
    check(near(p.x, 320) && near(p.y, 240), "native and fallback stretch");
    p = Map(320, 240, 640, 480, 0, 0, settings);
    check(near(p.x, 320) && near(p.y, 240), "missing backbuffer uses client");
    check(Map(0, 0, 0, 480, 640, 480, settings).x == -FLT_MAX, "minimized output invalidates cursor");
    check(FinalBlitRva(0x625C3202u) == 0x1FC1Fu && FinalBlitRva(123) == 0, "known wrapper identity only");
    unsigned char call[] = {0xE8, 0, 0, 0, 0, 0x90};
    int32_t delta = 0x20001000 - (0x1001FC1F + 5);
    std::memcpy(call + 1, &delta, 4);
    check(OwnsFinalBlit(call, 6, 0x1001FC1F, 0x20000000, 0x2000), "call targets display module");
    check(!OwnsFinalBlit(call, 5, 0x1001FC1F, 0x20000000, 0x2000), "truncated call refused");
    check(!OwnsFinalBlit(call, 6, 0x1001FC1F, 0x30000000, 0x2000), "unrelated hook refused");
    call[0] = 0xFF;
    check(!OwnsFinalBlit(call, 6, 0x1001FC1F, 0x20000000, 0x2000), "native indirect call refused");

    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = context->IO;
    int first = context->InputEventsQueue.Size;
    io.AddMousePosEvent(400, 200);
    ReplaceBackendMousePosition(*context, first, ImVec2(200, 100));
    io.AddMouseButtonEvent(0, true);
    io.AddMousePosEvent(200, 100);
    ImGui::UpdateInputEvents(true);
    check(io.MouseDown[0] && near(io.MousePos.x, 200), "click consumes mapped position in same frame");

    first = context->InputEventsQueue.Size;
    io.AddMousePosEvent(200, 100); // Backend deduplicates raw coordinates against mapped state.
    check(context->InputEventsQueue.Size == first, "dedup collision reproduced");
    ReplaceBackendMousePosition(*context, first, ImVec2(100, 50));
    ImGui::UpdateInputEvents(true);
    check(near(io.MousePos.x, 100), "deduplicated backend move still remaps");

    io.AddMouseButtonEvent(0, false);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
    ImGui::UpdateInputEvents(true);
    check(!io.MouseDown[0] && context->InputEventsQueue.Size == 2, "first fast release preserved");
    ImGui::UpdateInputEvents(true);
    check(io.MouseDown[0] && context->InputEventsQueue.Size == 1, "second fast press preserved");
    ImGui::UpdateInputEvents(true);
    check(!io.MouseDown[0] && context->InputEventsQueue.empty(), "second fast release preserved");

    io.AddMousePosEvent(50, 25);
    first = context->InputEventsQueue.Size;
    io.AddMousePosEvent(100, 50);
    io.AddKeyEvent(ImGuiKey_A, true);
    DiscardBackendMousePositions(*context, first);
    ImGui::UpdateInputEvents(true);
    check(near(io.MousePos.x, 50) && ImGui::GetKeyData(ImGuiKey_A)->Down, "fallback poll cannot overwrite mapped position or erase keyboard events");
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    ImGui::UpdateInputEvents(true);
    check(io.MousePos.x == -FLT_MAX, "mouse leave stays invalid");
    ImGui::DestroyContext(context);
    std::printf("pointer_mapping: %d failures\n", failures);
    return failures ? 1 : 0;
}
