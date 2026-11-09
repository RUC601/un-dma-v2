#include "Game/HeroSettings.hpp"
#include "ui/EditorSupport.hpp"
#include "Alerts/Service.hpp"
#include "Game/AbilityIcons.hpp"
#include "features/alerts/Speech.hpp"
#include <cstring>

namespace {
using namespace V2::Alerts;

bool EditMessage(const char* id, std::string& message, const char* hint, float width = -1) {
    char buffer[kMaxMessageBytes+1]{};
    std::memcpy(buffer, message.data(), (std::min)(message.size(), kMaxMessageBytes));
    ImGui::SetNextItemWidth(width > 0 ? width : (std::max)(1.0f, ImGui::GetContentRegionAvail().x - 8.0f));
    if (!ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer))) return false;
    message = buffer;
    return true;
}

void PreviewButton(const std::string& message, std::string& preview, bool zh) {
    if (ImGui::SmallButton(zh ? "试听###preview" : "Preview###preview")) preview = message;
}

void ReservedAlert(const char* title, bool zh) {
    UIGroupBox(title);
    bool enabled = false;
    ImGui::BeginDisabled();
    UICheckbox(zh ? "开启###enabled" : "Enable###enabled", &enabled);
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", zh ? "预留，等待接入检测" : "Detection pending");
    CloseGroupBox();
}
}

void UI::AlertPage() {
    using namespace V2::Alerts;
    const bool zh = OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified;
    const uint64_t localHero=CurrentHeroOption().heroId;
    auto heroSettings=V2::HeroSettings::Get(localHero);
    auto settings=heroSettings.alerts;
    const auto shared=GetSettings();
    settings.voice=shared.voice;settings.volume=shared.volume;settings.outputDevice=shared.outputDevice;
    bool edited = false;
    std::string preview;
    ImGui::PushID("AlertPage");
    UIGroupBox(zh ? "声音与输出" : "Voice and output");
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

    ImGui::BeginDisabled(localHero==0);
    edited|=UICheckbox(zh?"启用当前英雄预警":"Enable alerts for this hero",&heroSettings.alertsEnabled);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(localHero==0||!heroSettings.alertsEnabled);
    ImGui::PushID("skillAlert");
    UIGroupBox(zh ? "技能预警" : "Skill alerts");
    edited |= UICheckbox(zh ? "开启技能预警###enabled" : "Enable skill alerts###enabled", &settings.skillEnabled);
    ImGui::TextWrapped("%s", zh ? "敌方英雄使用所选技能时播报一次。" : "Announces once when an enemy uses a selected skill.");
    static uint64_t skillHero = kAnaHeroId;
    UIAlertHeroSelect("skillBrowse", skillHero, [&](uint64_t hero) {
        for (size_t i = 0; i < kDefinitions.size(); ++i)
            if (kDefinitions[i].hero == hero && settings.rules[i].enabled) return true;
        return false;
    });
    bool hasRules = false;
    float skillLabelWidth = 0;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    for (const auto& definition : kDefinitions)
        if (definition.hero == skillHero)
            skillLabelWidth = (std::max)(skillLabelWidth, ImGui::CalcTextSize(zh ? definition.skillNameZh : definition.skillName).x);
    skillLabelWidth += ControlHeight() + gap;
    for (size_t i = 0; i < kDefinitions.size(); ++i) {
        const auto& definition = kDefinitions[i];
        if (definition.hero != skillHero) continue;
        hasRules = true;
        auto& rule = settings.rules[i];
        ImGui::PushID(definition.id);
        ImGui::Spacing();
        const char* label = zh ? definition.skillNameZh : definition.skillName;
        const float width = ImGui::GetContentRegionAvail().x;
        const float previewWidth = ImGui::CalcTextSize(zh ? "试听" : "Preview").x + ImGui::GetStyle().FramePadding.x * 2;
        const bool inlineRow = width >= skillLabelWidth + previewWidth + ImGui::GetFontSize() * 12 + gap * 2;
        edited |= UICheckbox(label, &rule.enabled);
        if (inlineRow) ImGui::SameLine(skillLabelWidth + gap + ImGui::GetCursorPosX());
        const auto inputId = std::string("##message-") + definition.id;
        edited |= EditMessage(inputId.c_str(), rule.message, definition.defaultMessage,
            inlineRow ? (std::max)(1.0f, ImGui::GetContentRegionAvail().x - previewWidth - gap - 8.0f) : -1);
        if (inlineRow) ImGui::SameLine();
        PreviewButton(rule.message.empty() ? definition.defaultMessage : rule.message, preview, zh);
        ImGui::PopID();
    }
    if (!hasRules) {
        for (const auto& hero : kHeroOptions) {
            if (hero.heroId != skillHero) continue;
            if (const auto* abilities = OW::GetAbilityIcons(hero.label)) {
                for (const auto* ability : {abilities->ability1Icon, abilities->ability2Icon, abilities->ability3Icon}) {
                    if (!ability) continue;
                    std::string label = ability;
                    std::replace(label.begin(), label.end(), '-', ' ');
                    if (!label.empty() && label.front() >= 'a' && label.front() <= 'z') label.front() -= 'a' - 'A';
                    ImGui::TextDisabled("%s", label.c_str());
                }
            }
            break;
        }
        ImGui::TextWrapped("%s", zh ? "该英雄还没有可用的技能预警。" : "Skill alerts are unavailable for this hero.");
    }
    CloseGroupBox();
    ImGui::PopID();

    ImGui::PushID("flankAlert");
    ReservedAlert(zh ? "绕后预警" : "Flanking warning", zh);
    ImGui::PopID();
    ImGui::PushID("aimThreatAlert");
    ReservedAlert(zh ? "被瞄准预警" : "Being aimed at", zh);
    ImGui::PopID();

    ImGui::PushID("ultimateAlert");
    UIGroupBox(zh ? "大招预警" : "Ultimate alerts");
    edited |= UICheckbox(zh ? "开启大招预警###enabled" : "Enable ultimate alerts###enabled", &settings.ultimateEnabled);
    ImGui::TextWrapped("%s", zh ? "敌方大招充能达到 100% 时播报一次。" : "Announces once when an enemy's ultimate charge reaches 100%.");
    ImGui::PushID("global");
    edited |= UICheckbox(zh ? "全局默认开启###all" : "Enabled for all by default###all", &settings.ultimateAll);
    ImGui::TextUnformatted(zh ? "全局播报文案" : "Default announcement");
    edited |= EditMessage("##ultimateDefault", settings.ultimateMessage, kDefaultUltimateMessage);
    PreviewButton(settings.ultimateMessage.empty() ? kDefaultUltimateMessage : settings.ultimateMessage, preview, zh);
    ImGui::PopID();
    ImGui::Spacing();
    static uint64_t ultimateHero = 0;
    UIAlertHeroSelect("ultimateBrowse", ultimateHero, [&](uint64_t hero) { return UltimateHeroEnabled(settings, hero); });
    if (UltimateHeroSupported(ultimateHero)) {
        const auto* existing = UltimateForHero(settings, ultimateHero);
        auto rule = existing ? *existing : UltimateRule{ultimateHero};
        const char* modesZh[]{settings.ultimateAll ? "跟随全局（开）" : "跟随全局（关）", "单独关闭", "单独开启"};
        const char* modesEn[]{settings.ultimateAll ? "Use default (on)" : "Use default (off)", "Always off", "Always on"};
        int mode = rule.mode + 1;
        ImGui::PushID("hero");
        ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - 8.0f));
        bool changed = UISelect("##heroMode", &mode, zh ? modesZh : modesEn, 3);
        rule.mode = mode - 1;
        ImGui::TextUnformatted(zh ? "英雄专属文案（留空使用全局）" : "Hero announcement (blank uses default)");
        changed |= EditMessage("##ultimateHeroMessage", rule.message, settings.ultimateMessage.c_str());
        PreviewButton(rule.message.empty() ? settings.ultimateMessage : rule.message, preview, zh);
        if (changed) {
            if (existing) settings.ultimates[size_t(existing - settings.ultimates.data())] = std::move(rule);
            else settings.ultimates.push_back(std::move(rule));
            edited = true;
        }
        ImGui::PopID();
    }
    CloseGroupBox();
    ImGui::PopID();

    ImGui::EndDisabled();
    if(edited) {
        auto global=GetSettings();global.voice=settings.voice;global.volume=settings.volume;global.outputDevice=settings.outputDevice;
        SetSettings(std::move(global));
        heroSettings.alerts=std::move(settings);V2::HeroSettings::Set(localHero,std::move(heroSettings));
    }
    if (!preview.empty()) Preview(std::move(preview));
    ImGui::PopID();
}
