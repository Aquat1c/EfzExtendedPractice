#include "gui/custom_menu/layout.h"
#include "gui/custom_menu/scale.h"
#include "imgui_internal.h"
#include <cstdio>
#include <algorithm>
#include <initializer_list>

namespace CustomMenu::Fonts {
ImFont* Body() { return ImGui::GetIO().Fonts->Fonts[0]; }
ImFont* Header() { return Body(); }
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.Fonts->AddFontDefault();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
    };
    const char* labels[] = { "Dummy", "Recording", "Frame data", "Settings", "Missions" };
    for (float scale : {0.75f, 1.0f, 1.25f, 1.5f}) {
        CustomMenu::Scale::Update(scale);
        ImGui::GetIO().Fonts->Fonts[0]->FontSize = 13.0f * scale;
        for (float barWidth : {250.0f, 560.0f}) {
            for (float y : {40.0f, 66.0f}) {
                for (int selected = 0; selected < 5; ++selected) {
                    ImDrawList draw(ImGui::GetDrawListSharedData());
                    draw._ResetForNewFrame();
                    draw.PushTextureID(io.Fonts->TexID);
                    draw.PushClipRectFullScreen();
                    CustomMenu::Layout::DrawTabBar(&draw, 40.0f, y, barWidth,
                        labels, 5, selected, -1);
                    const ImU32 fill = IM_COL32(0, 0, 0, 118);
                    ImVec2 tl(FLT_MAX, FLT_MAX), br(-FLT_MAX, -FLT_MAX);
                    for (const auto& vertex : draw.VtxBuffer) {
                        if (vertex.col != fill) continue;
                        tl.x = (std::min)(tl.x, vertex.pos.x);
                        tl.y = (std::min)(tl.y, vertex.pos.y);
                        br.x = (std::max)(br.x, vertex.pos.x);
                        br.y = (std::max)(br.y, vertex.pos.y);
                    }
                    check(tl.x < br.x && tl.y < br.y, "active tab has a drawn rectangle");
                    for (ImVec2 p : {ImVec2(tl.x + 0.25f, tl.y + 0.25f),
                                    ImVec2(br.x - 0.25f, br.y - 0.25f),
                                    ImVec2((tl.x + br.x) * 0.5f, (tl.y + br.y) * 0.5f)}) {
                        check(CustomMenu::Layout::TabBarIndexAt(40.0f, y, barWidth,
                            labels, 5, p) == selected, "drawn tab corners and center hit the same tab");
                    }
                    check(CustomMenu::Layout::TabBarIndexAt(40.0f, y, barWidth,
                        labels, 5, ImVec2((tl.x + br.x) * 0.5f, tl.y - 1.0f)) == -1,
                        "above the drawn tab is not clickable");
                }
            }
        }
    }
    check(CustomMenu::Layout::TabBarIndexAt(0, 0, 100, nullptr, 0, ImVec2(0, 0)) == -1,
        "empty tab bar has no hit");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf("menu tab geometry: %d failures\n", failures);
    return failures ? 1 : 0;
}
