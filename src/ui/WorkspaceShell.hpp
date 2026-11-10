#pragma once
#include <functional>
#include <span>
#include <string>
#include <imgui.h>

namespace UI::Workspace {
struct Page { const char* label; const char* group; };
// 壳只拥有布局；配置编辑、保存和页面业务通过回调交给各自所有者。
struct Model {
    std::span<const Page> pages;
    int selected = 0;
    const char* saveLabel = "保存全部";
    std::string status;
    bool saveFailed = false;
    ImTextureID brandIcon = 0;
    float headerWidth = 0;
    float headerColumnWidth = 0;
    float statusAge = 0;
};
void Render(Model& model, const std::function<void()>& save,
            const std::function<void(int)>& body,
            const std::function<void(ImDrawList*, ImVec2, float)>& footer,
            const std::function<void(float, bool)>& header = {});
}
