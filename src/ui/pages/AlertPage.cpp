#include "ui/EditorSupport.hpp"
#include "Alerts/Service.hpp"
#include "features/alerts/Speech.hpp"
#include <cstring>

void UI::AlertPage() {
    using namespace V2::Alerts;
    const bool zh = OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified;
    auto settings = GetSettings();
    bool edited = false;
    static int module = 0;
    static size_t selectedRule = 0;
    selectedRule = (std::min)(selectedRule, settings.rules.size()-1);
    ImGui::PushID("AlertPage");
    UIGroupBox("Alert");
    edited |= UICheckbox(zh ? "启用预警" : "Enable alerts", &settings.enabled);
    ImGui::TextWrapped("%s", zh ? "预警适用于所有敌方英雄，独立于本地英雄的瞄准和技能方案。"
        : "Alerts watch enemy heroes and use shared settings across your own hero schemes.");
    const char* modulesZh[]{"技能语音播报", "绕后预警", "被瞄准预警", "大招播报"};
    const char* modulesEn[]{"Skill voice alerts", "Flanking warning", "Being aimed at", "Ultimate announcements"};
    SettingRow(zh ? "预警模块" : "Alert module", kDefaultLabelWidth);
    PushControlWidth();
    UISelect("##module", &module, zh ? modulesZh : modulesEn, 4);
    ImGui::PopItemWidth();
    CloseGroupBox();
    if (module != 0) {
        UIGroupBox(zh ? modulesZh[module] : modulesEn[module]);
        ImGui::TextWrapped("%s", zh ? "已预留入口，暂未实现。当前版本提供技能语音播报。"
            : "Planned. This release provides skill voice alerts.");
        CloseGroupBox();
    } else {
        UIGroupBox(zh ? "英雄与技能" : "Heroes and skills");
        if (ImGui::BeginTable("SkillRules", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(zh ? "英雄" : "Hero", 0, 1);
            ImGui::TableSetupColumn(zh ? "技能" : "Skill", 0, 1.4f);
            ImGui::TableSetupColumn(zh ? "播报" : "Alert", 0, .7f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < kDefinitions.size(); ++i) {
                ImGui::PushID(kDefinitions[i].id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(zh ? kDefinitions[i].heroNameZh : kDefinitions[i].heroName, selectedRule == i)) selectedRule = i;
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", zh ? kDefinitions[i].skillNameZh : kDefinitions[i].skillName);
                ImGui::TableNextColumn();
                const auto checkboxId = std::string("##") + kDefinitions[i].id;
                if (UICheckbox(checkboxId.c_str(), &settings.rules[i].enabled)) { edited = true; selectedRule = i; }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("%s", zh ? "敌方英雄使用所选技能后播报一次。首次发现和数据中断后先建立基准。"
            : "Announces once when an enemy uses the selected skill. First sightings and observation gaps establish a baseline.");
        SettingRow(zh ? "预警内容" : "Announcement", kDefaultLabelWidth);
        char message[kMaxMessageBytes+1]{};
        std::memcpy(message, settings.rules[selectedRule].message.data(), settings.rules[selectedRule].message.size());
        PushControlWidth();
        if (ImGui::InputText("##message", message, sizeof(message))) {
            settings.rules[selectedRule].message = message;
            edited = true;
        }
        ImGui::PopItemWidth();
        if (edited) SetSettings(settings);
        if (ImGui::Button(zh ? "试听播报" : "Preview announcement", ImVec2(0, ControlHeight()))) Preview(kDefinitions[selectedRule].id);
        CloseGroupBox();
    }
    UIGroupBox(zh ? "声音与输出" : "Voice and output");
    SettingRow(zh ? "播报声音" : "Voice", kDefaultLabelWidth);
    const char* voicesZh[]{"中文女声", "中文男声"};
    const char* voicesEn[]{"Chinese / Female", "Chinese / Male"};
    PushControlWidth();
    edited |= UISelect("##voice", &settings.voice, zh ? voicesZh : voicesEn, 2);
    ImGui::PopItemWidth();
    SettingRow(zh ? "音量" : "Volume", kDefaultLabelWidth);
    PushControlWidth();
    edited |= ImGui::SliderInt("##volume", &settings.volume, 0, 100, "%d%%");
    ImGui::PopItemWidth();
    static auto devices = PlaybackDevices();
    SettingRow(zh ? "输出设备" : "Playback device", kDefaultLabelWidth);
    PushControlWidth();
    const auto foundDevice = std::find_if(devices.begin(), devices.end(), [&](const auto& item) { return item.id == settings.outputDevice; });
    const char* selected = settings.outputDevice.empty() ? (zh ? "系统默认播放设备" : "System playback default")
        : foundDevice != devices.end() ? foundDevice->name.c_str() : (zh ? "所选设备不可用" : "Selected device unavailable");
    if (ImGui::BeginCombo("##device", selected)) {
        for (const auto& device : devices) {
            const char* label = device.name.empty() ? (zh ? "系统默认播放设备" : "System playback default") : device.name.c_str();
            ImGui::PushID(device.id.c_str());
            if (ImGui::Selectable(label, device.id == settings.outputDevice)) {
                settings.outputDevice = device.id; edited = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (edited) SetSettings(settings);
    if (ImGui::Button(zh ? "刷新设备与声音" : "Refresh devices and voice", ImVec2(0, ControlHeight()))) {
        devices = PlaybackDevices(); RetrySpeech();
    }
    const auto speech = GetStatus();
    const char* state = zh ? "声音未启动" : "Voice stopped";
    switch (speech.state) {
    case SpeechState::Preparing: state = zh ? "正在准备离线声音…" : "Preparing offline voice..."; break;
    case SpeechState::Ready: state = zh ? "离线声音已就绪" : "Offline voice ready"; break;
    case SpeechState::Speaking: state = zh ? "正在播报" : "Speaking"; break;
    case SpeechState::Failed:
        state = speech.error == "voice_missing"
            ? (zh ? "离线声音资源未安装，请先完成声音资源准备。" : "Offline voice files are missing. Prepare the voice resources first.")
            : speech.error == "audio_device_missing"
            ? (zh ? "所选输出设备不可用，请重新选择。" : "Selected playback device is unavailable. Choose another device.")
            : (zh ? "声音准备或播放失败，请刷新后重试。" : "Could not prepare or play the voice. Refresh and retry.");
        break;
    default: break;
    }
    ImGui::TextWrapped("%s", state);
    ImGui::Spacing();
    ImGui::TextWrapped("%s", zh ? "使用离线 Kokoro 中文声音。传给 Oopz 队友：此处选择虚拟音频线的播放端，副机 Oopz 的麦克风选择同一音频线的录音端。"
        : "Uses offline Kokoro Chinese voices. For Oopz, choose the virtual cable playback end here and its recording end as Oopz's microphone on this PC.");
    CloseGroupBox();
    ImGui::PopID();
}
