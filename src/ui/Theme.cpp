#include "Theme.hpp"
#include "config/UiPreferences.hpp"
#include "Utils/Config.hpp"
namespace UI::Theme {
ImFont* s_regularFont = nullptr;
ImFont* s_boldFont = nullptr;
ImFont* s_titleFont = nullptr;
bool Initialize() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigErrorRecoveryEnableTooltip = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!s_regularFont && !io.Fonts->Locked) {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;
        fontConfig.RasterizerMultiply = 1.12f;
        auto addUiFont = [&](const char* primaryPath, const char* fallbackPath, float size) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(primaryPath, size, &fontConfig);
            if (font) {
                ImFontConfig fallbackConfig;
                fallbackConfig.MergeMode = true;
                fallbackConfig.OversampleH = fontConfig.OversampleH;
                fallbackConfig.OversampleV = fontConfig.OversampleV;
                fallbackConfig.PixelSnapH = fontConfig.PixelSnapH;
                fallbackConfig.RasterizerMultiply = fontConfig.RasterizerMultiply;
                io.Fonts->AddFontFromFileTTF(fallbackPath, size, &fallbackConfig,
                                             io.Fonts->GetGlyphRangesChineseFull());
            }
            return font;
        };
        s_regularFont = addUiFont("C:\\Windows\\Fonts\\segoeui.ttf",
                                  "C:\\Windows\\Fonts\\msyh.ttc", 15.0f);
        s_boldFont = addUiFont("C:\\Windows\\Fonts\\segoeuib.ttf",
                               "C:\\Windows\\Fonts\\msyhbd.ttc", 15.0f);
        s_titleFont = addUiFont("C:\\Windows\\Fonts\\segoeuib.ttf",
                                "C:\\Windows\\Fonts\\msyhbd.ttc", 18.0f);
        if (!s_titleFont)
            s_titleFont = s_boldFont;
        if (s_regularFont)
            io.FontDefault = s_regularFont;
    } else if (s_regularFont) {
        io.FontDefault = s_regularFont;
    }

    Preferences::Load(OW::Config::uiLanguage);
    Apply();
    return s_regularFont != nullptr || !io.Fonts->Locked;
}
void Apply() {
    const auto& preferences = Preferences::values;
    const bool graphite = preferences.skin == 1, violet = preferences.skin == 2;
    kColShell0 = graphite ? IM_COL32(21,23,25,255) : violet ? IM_COL32(15,16,19,255) : IM_COL32(16,17,21,255);
    kColShell1 = graphite ? IM_COL32(23,25,28,255) : violet ? IM_COL32(17,18,22,255) : IM_COL32(18,19,24,255);
    kColShell2 = IM_COL32(11,12,16,255);
    kColPanel = graphite ? IM_COL32(29,32,36,255) : violet ? IM_COL32(22,23,29,255) : IM_COL32(25,27,33,255);
    kColPanelSoft = (kColPanel & ~IM_COL32_A_MASK) | ((preferences.consoleLayers ? 225u : 255u) << IM_COL32_A_SHIFT);
    kColControl = graphite ? IM_COL32(40,44,49,255) : violet ? IM_COL32(32,33,43,255) : IM_COL32(34,37,45,255);
    kColControlHover = graphite ? IM_COL32(48,53,60,255) : violet ? IM_COL32(44,43,57,255) : IM_COL32(43,47,57,255);
    kColControlHot = graphite ? IM_COL32(57,62,71,255) : violet ? IM_COL32(51,47,67,255) : IM_COL32(51,55,66,255);
    kColStroke = graphite ? IM_COL32(65,70,79,255) : violet ? IM_COL32(54,54,67,255) : IM_COL32(52,56,68,255);
    kColText = IM_COL32(240,241,245,255);
    kColTextMuted = IM_COL32(218,221,230,255);
    kColTextDim = IM_COL32(169,174,187,255);
    kColAccent = preferences.customAccent ? ImGui::ColorConvertFloat4ToU32(preferences.accent) :
        graphite ? IM_COL32(190,200,220,255) : violet ? IM_COL32(171,147,255,255) : IM_COL32(228,17,67,255);
    ImVec4 accent = ImGui::ColorConvertU32ToFloat4(kColAccent);
    kColAccentDark = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x*.28f,accent.y*.28f,accent.z*.28f,1));
    kColAccentSoft = (kColAccent & ~IM_COL32_A_MASK) | (56u << IM_COL32_A_SHIFT);
    kColAccentGlow = (kColAccent & ~IM_COL32_A_MASK) | (28u << IM_COL32_A_SHIFT);
    ImGuiStyle& style   = ImGui::GetStyle();
    const float scale=(std::max)(1.f,style.FontScaleDpi);
    style.AntiAliasedLines = true;
    style.AntiAliasedFill  = true;
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = graphite ? 5.0f : 6.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = 8.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(0, 0);
    style.FramePadding      = ImVec2(8*scale,4*scale);
    style.ItemSpacing       = ImVec2(8*scale,6*scale);
    style.ItemInnerSpacing  = ImVec2(6*scale,4*scale);
    style.ScrollbarSize     = 7.0f*scale;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]          = ImGui::ColorConvertU32ToFloat4(kColShell0);
    colors[ImGuiCol_FrameBg]           = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_FrameBgHovered]    = ImGui::ColorConvertU32ToFloat4(kColControlHover);
    colors[ImGuiCol_FrameBgActive]     = ImGui::ColorConvertU32ToFloat4(kColControlHot);
    colors[ImGuiCol_Button]            = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_ButtonHovered]     = ImGui::ColorConvertU32ToFloat4(kColControlHover);
    colors[ImGuiCol_ButtonActive]      = ImGui::ColorConvertU32ToFloat4(kColControlHot);
    colors[ImGuiCol_Header]            = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_HeaderHovered]     = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_HeaderActive]      = ImGui::ColorConvertU32ToFloat4(kColAccentDark);
    colors[ImGuiCol_CheckMark]         = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_SliderGrab]        = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_SliderGrabActive]  = ImGui::ColorConvertU32ToFloat4(IM_COL32(0xf0, 0x1a, 0x49, 0xFF));
    colors[ImGuiCol_Text]              = ImGui::ColorConvertU32ToFloat4(kColText);
    colors[ImGuiCol_TextDisabled]      = ImGui::ColorConvertU32ToFloat4(kColTextDim);
    colors[ImGuiCol_Border]            = ImGui::ColorConvertU32ToFloat4(kColStroke);
    colors[ImGuiCol_TitleBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_TitleBgActive]     = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_Separator]         = ImGui::ColorConvertU32ToFloat4(kColStroke);
    colors[ImGuiCol_ScrollbarBg]       = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x0a, 0x0b, 0x0e, 0xE0));
    colors[ImGuiCol_ScrollbarGrab]     = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x34, 0x37, 0x40, 0xD8));
    colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_PopupBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.86f, 0.40f, 1.0f);
    colors[ImGuiCol_TextSelectedBg]    = ImGui::ColorConvertU32ToFloat4(kColAccentSoft);
}

}
