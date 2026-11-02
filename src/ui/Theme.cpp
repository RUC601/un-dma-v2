#include "Theme.hpp"
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

    ImGuiStyle& style   = ImGui::GetStyle();
    style.AntiAliasedLines = true;
    style.AntiAliasedFill  = true;
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = 4.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(0, 0);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.ScrollbarSize     = 7.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]          = ImGui::ColorConvertU32ToFloat4(kColPanel);
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
    colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(0xe4, 0x11, 0x43, 0xC8));
    colors[ImGuiCol_PopupBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.86f, 0.40f, 1.0f);
    colors[ImGuiCol_TextSelectedBg]    = ImGui::ColorConvertU32ToFloat4(kColAccentSoft);
    return s_regularFont != nullptr || !io.Fonts->Locked;
}

}
