#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace UI::Slots {
// 页面提交只读视图；组件仅返回用户命令，不拥有配置或调用设备。
struct Item { uint64_t id; std::string name; bool enabled; };
enum class Action { None, Select, Add, Remove };
struct Command { Action action=Action::None; int index=0; };
Command Render(std::span<const Item> items, int selected, int limit, bool chinese);
}
