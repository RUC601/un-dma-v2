#pragma once
#include "Features/UI.hpp"
#include "Features/UiValueFormat.hpp"
#include "Utils/Config.hpp"
#include "Game/Structs.hpp"
#include "Game/HeroSkills.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
#include <functional>
#include <string>
#include <vector>
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
using namespace UI::ValueFormat;
// UI 线程专用：state 是编辑上下文；这些接口不向采集线程暴露 ImGui。
struct HeroOption {
    const char* label;
    uint64_t heroId;
    const char* category;
};

inline const HeroOption kHeroOptions[] = {
    { "All", 0, "Global" },
    { "Reinhardt", OW::eHero::HERO_REINHARDT, "Tank" },
    { "Winston", OW::eHero::HERO_WINSTON, "Tank" },
    { "Zarya", OW::eHero::HERO_ZARYA, "Tank" },
    { "DVa", OW::eHero::HERO_DVA, "Tank" },
    { "Roadhog", OW::eHero::HERO_ROADHOG, "Tank" },
    { "Orisa", OW::eHero::HERO_ORISA, "Tank" },
    { "WreckingBall", OW::eHero::HERO_WRECKINGBALL, "Tank" },
    { "Sigma", OW::eHero::HERO_SIGMA, "Tank" },
    { "Doomfist", OW::eHero::HERO_DOOMFIST, "Tank" },
    { "Ramattra", OW::eHero::HERO_RAMATTRA, "Tank" },
    { "JunkerQueen", OW::eHero::HERO_JUNKERQUEEN, "Tank" },
    { "Mauga", OW::eHero::HERO_MAUGA, "Tank" },
    { "Hazard", OW::eHero::HERO_HAZARD, "Tank" },
    { "Tracer", OW::eHero::HERO_TRACER, "Damage" },
    { "Widowmaker", OW::eHero::HERO_WIDOWMAKER, "Damage" },
    { "Soldier76", OW::eHero::HERO_SOLDIER76, "Damage" },
    { "Genji", OW::eHero::HERO_GENJI, "Damage" },
    { "Hanzo", OW::eHero::HERO_HANJO, "Damage" },
    { "Cassidy", OW::eHero::HERO_MCCREE, "Damage" },
    { "Pharah", OW::eHero::HERO_PHARAH, "Damage" },
    { "Reaper", OW::eHero::HERO_REAPER, "Damage" },
    { "Sombra", OW::eHero::HERO_SOMBRA, "Support" },
    { "Symmetra", OW::eHero::HERO_SYMMETRA, "Damage" },
    { "Torbjorn", OW::eHero::HERO_TORBJORN, "Damage" },
    { "Bastion", OW::eHero::HERO_BASTION, "Damage" },
    { "Junkrat", OW::eHero::HERO_JUNKRAT, "Damage" },
    { "Mei", OW::eHero::HERO_MEI, "Damage" },
    { "Ashe", OW::eHero::HERO_ASHE, "Damage" },
    { "Sojourn", OW::eHero::HERO_SOJOURN, "Damage" },
    { "Venture", OW::eHero::HERO_VENTURE, "Damage" },
    { "Echo", OW::eHero::HERO_ECHO, "Damage" },
    { "Emre", OW::eHero::HERO_EMRE, "Damage" },
    { "Freja", OW::eHero::HERO_FREJA, "Damage" },
    { "Sierra", OW::eHero::HERO_SIERRA, "Damage" },
    { "Vendetta", OW::eHero::HERO_VENDETTA, "Damage" },
    { "Anran", OW::eHero::HERO_ANRAN, "Damage" },
    { "Shion", OW::eHero::HERO_SHION, "Damage" },
    { "Mercy", OW::eHero::HERO_MERCY, "Support" },
    { "Lucio", OW::eHero::HERO_LUCIO, "Support" },
    { "Zenyatta", OW::eHero::HERO_ZENYATTA, "Support" },
    { "Ana", OW::eHero::HERO_ANA, "Support" },
    { "Brigitte", OW::eHero::HERO_BRIGITTE, "Support" },
    { "Moira", OW::eHero::HERO_MOIRA, "Support" },
    { "Baptiste", OW::eHero::HERO_BAPTISTE, "Support" },
    { "Kiriko", OW::eHero::HERO_KIRIKO, "Support" },
    { "Lifeweaver", OW::eHero::HERO_LIFEWEAVER, "Support" },
    { "Illari", OW::eHero::HERO_ILLARI, "Support" },
    { "Mizuki", OW::eHero::HERO_MIZUKI, "Support" },
    { "Juno", OW::eHero::HERO_JUNO, "Support" },
    { "Wuyang", OW::eHero::HERO_WUYANG, "Support" },
    { "JetpackCat", OW::eHero::HERO_JETPACKCAT, "Support" },
    { "Domina", OW::eHero::HERO_DOMINA, "Tank" },
    { "D.Mon", OW::eHero::HERO_DMON, "Tank" },
};
inline const char* kHeroSkillModes[] = {
    "Auto", "Assist", "Manual"
};
inline const char* kHeroSkillTrackingBones[] = {
    "Chest", "Head", "Neck"
};
inline const char* kHeroSkillTargetBones[] = {
    "Chest", "Head", "Neck", "Closest"
};
inline const char* kAimBehavior[]  = { "Tracking", "Flick", "Flick2nd", "Reacquire", "MagneticTrigger" };
inline const char* kAimMethod[]    = { "Linear", "PID", "Bezier", "Piecewise", "Accel Limited", "Constant", "Hybrid PID", "Shuwu", "Error response curve" };
inline const char* kAimSmoothType[] = { "Constant Speed", "Linear", "Bezier" };
inline const char* kPredictionMode[] = { "Auto", "Force On", "Force Off" };
inline const char* kFirePolicy[] = {
    "Manual", "Hold Tracking", "Tap Hit Window", "Release Delay", "Timed Burst", "Charge Release"
};
inline const char* kPriority[]     = { "Lowest FOV", "Lowest HP", "Distance", "Smart" };
inline const char* kTeam[]         = { "Enemies", "Allies", "All" };
inline const char* kTrace[]        = { "Strict", "Relaxed", "Off" };
inline const char* kUnlock[]       = { "Anytime", "On Release", "Never" };
inline const char* kOutputDeviceTypes[] = { "KMBox Net", "KMBox Serial", "Mock", "MAKCU 4M", "MAKCU 115200", "Ferrum Software API", "CatBox" };
inline const char* kMockFaultModes[] = { "None", "Output Timeout", "Drop Output" };
inline const char* kMenuToggleKeys[] = {
    "Home", "Insert", "End", "Delete",
    "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12"
};
inline constexpr int kMenuToggleVk[] = {
    VK_HOME, VK_INSERT, VK_END, VK_DELETE,
    VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6,
    VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12
};


enum class ActionSlotKind {
    Aim,
    Trigger
};

inline constexpr float kShellWidth = 1000.0f;
inline constexpr float kShellBorder = 2.0f;
inline constexpr float kHeaderHeight = 86.0f;
inline constexpr float kMinShellHeight = 140.0f;
inline constexpr float kMaxShellHeight = 1200.0f;
inline constexpr float kBodyBottomPadding = 10.0f;
inline constexpr float kDefaultLabelWidth = 120.0f;
inline constexpr float kAimbotHeroLabelWidth = 98.0f;
inline constexpr float kAimbotLeftLabelWidth = 134.0f;
inline constexpr float kAimbotRightLabelWidth = 134.0f;
inline float ControlHeight() { return (std::max)(26.0f, ImGui::GetFontSize() + 10.0f); }
inline bool UIActionButton(const char* label,float minimumWidth) {
    float width=(std::max)(minimumWidth,ImGui::CalcTextSize(label).x+20.0f);
    if(ImGui::GetContentRegionAvail().x<width) ImGui::NewLine();
    width=(std::min)(width,ImGui::GetContentRegionAvail().x);
    return ImGui::Button(label,ImVec2(width,ControlHeight()));
}
inline constexpr float kControlRounding = 4.0f;
inline constexpr float kGroupRounding = 8.0f;
inline constexpr float kGroupContentIndent = 14.0f;
inline constexpr float kGroupBorderClipInset = 2.0f;
inline constexpr float kGroupDefaultBottomGap = 7.0f;
inline constexpr float kActionSlotBottomGap = 10.0f;
inline constexpr float kControlRightPadding = 10.0f;
inline constexpr int kAimingSubTabCount = 4;
inline constexpr int kThemeSubTabCount = 2;
inline constexpr int kMiscSubTabCount = 7;
inline constexpr int kVisualsPageKey = kAimingSubTabCount;
inline constexpr int kThemePageKeyBase = kVisualsPageKey + 1;
inline constexpr int kMiscPageKeyBase = kThemePageKeyBase + kThemeSubTabCount;
inline constexpr int kMeasuredPageCount = kMiscPageKeyBase + kMiscSubTabCount;


inline constexpr size_t kConfigProfileNameBufferSize = 64;
    struct OutputConnectionTestResult {
        bool ok = false;
        std::string message;
    };

namespace UI::EditorCommands {
std::string GetFirePatternRecorderStatus(bool& interrupted);
bool IsFirePatternRecorderRunning();
void StartFirePatternRecorder();
void StopFirePatternRecorder();
OutputConnectionTestResult AllowOutputMonitorFirewall();
OutputConnectionTestResult RestartOutputNetworkAdapter();
OutputConnectionTestResult TestOutputConnection();
OutputConnectionTestResult ApplyOutputRuntimeFromCurrentConfig(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
void CopyConfigProfileName(char (&destination)[kConfigProfileNameBufferSize], const std::string& name);
std::string NormalizeProfileFileName(const char* rawName);
bool SelectConfigProfile(const char* name, char (&profileName)[kConfigProfileNameBufferSize],
                             bool saveCurrent = true);
std::vector<std::string> EnumerateConfigProfiles();
bool CreateConfigProfileFromCurrent(const std::string& profileName);
int FindConfigProfileIndex(const std::vector<std::string>& profiles, const std::string& name);
bool PersistLastConfigProfile();
}
using namespace UI::EditorCommands;
const char* T(UiText id);
const char* L(const char* value);
void DrawAimHotkeyProbe();
int ActiveHeroPresetSlotIndex(ActionSlotKind kind);
bool IsHeroActionSlotEnabled(ActionSlotKind kind, uint64_t heroId, int slotIndex);
OW::Config::HeroPreset MakeCurrentHeroActionPreset(ActionSlotKind kind);
OW::Config::HeroPreset GetHeroActionPresetOrDefault(ActionSlotKind kind,
                                                           uint64_t heroId,
                                                           int slotIndex);
void SetHeroActionPreset(ActionSlotKind kind,
                                uint64_t heroId,
                                int slotIndex,
                                const OW::Config::HeroPreset& preset);
void ApplyHeroActionPresetToGlobals(ActionSlotKind kind, const OW::Config::HeroPreset& preset);
void PushHeroActionSlotControlId(ActionSlotKind kind, uint64_t heroId, int slotIndex);
void SetPersistenceStatus(bool saved, const char* successText);
float MaxFloat(float a, float b);
int FindMenuToggleKeyIndex(int vk);
int ClampHeroSelectionIndex(int index);
const HeroOption& CurrentHeroOption();
ID3D11ShaderResourceView* HeroSkillIconForDefinition(const OW::HeroSkillDefinition& definition);
void ApplySelectedTypePreset();
bool UIGroupBox(const char* title, float minHeightPx = 0.0f, float bottomGapPx = kGroupDefaultBottomGap);
void CloseGroupBox();
bool UICheckbox(const char* label, bool* value);
void PushControlWidth(float rightPaddingPx = kControlRightPadding);
bool UISlider(const char* label, float* value, float v_min, float v_max,
                     const Spec& format, bool integralValue = false);
bool UISlider(const char* label, int* value, float v_min, float v_max,
                     const Spec& format);
bool UISelect(const char* label, int* current, const char* const items[], int itemCount, bool translateItems = true);
bool UIHeroMultiSelect(const char* id, std::vector<uint64_t>& heroes,
                       const OW::AimPlan::Plan* plan = nullptr, uint64_t ownGroup = 0);
template <std::size_t Count>
bool UISelectMapped(const char* label,
                           int* currentValue,
                           const char* const (&items)[Count],
                           const int (&mappedValues)[Count],
                           int fallbackValue) {
    int selection = -1;
    int fallbackSelection = 0;
    for (std::size_t index = 0; index < Count; ++index) {
        if (mappedValues[index] == *currentValue)
            selection = static_cast<int>(index);
        if (mappedValues[index] == fallbackValue)
            fallbackSelection = static_cast<int>(index);
    }

    const bool repairedUnsupportedValue = selection < 0;
    if (repairedUnsupportedValue)
        selection = fallbackSelection;

    const bool selected = UISelect(
        label,
        &selection,
        items,
        static_cast<int>(Count));
    if (selected || repairedUnsupportedValue) {
        *currentValue = mappedValues[static_cast<std::size_t>(selection)];
        return true;
    }
    return false;
}
bool UISkeletonBoneMultiSelect(const char* label, OW::SkeletonBoneMask* rawMask);
int NextAimMethodPresetId();
int NextDynamicFovPresetId();
std::string NormalizeUiPresetName(const char* text, const char* fallback, int index);
void CopyPresetNameToBuffer(const std::string& name, char (&buffer)[64]);
OW::Config::AimMethodPreset CaptureMethodPresetFromCurrent(int method, const char* name);
OW::Config::AimBehaviorPreset CaptureBehaviorPresetFromCurrent(int behavior,
                                                                      int method,
                                                                      int methodPresetId,
                                                                      float baseSpeed,
                                                                      const OW::AimStartLimiterProfile& startLimiter,
                                                                      bool moveSplitEnabled,
                                                                      int moveSplitMaxPixels,
                                                                      int moveSplitDelayUs,
                                                                      const char* name);
bool UISelectAimMethodPreset(const char* label, int* presetId, int filterMethod);
bool UISelectAimBehaviorPreset(const char* label, int* presetId, int filterBehavior, const char* noneText="Class Default");
bool UISelectDynamicFovPreset(const char* label, int* presetId, bool allowNone);
void ApplyAimBehaviorToHeroPreset(OW::Config::HeroPreset& preset, int behavior);
void UIDisabledSelect(const char* label, const char* previewText = nullptr);
bool UIAttackActionSelect(const char* label, int* action, uint64_t heroId);
bool UIActionSlotSelector(ActionSlotKind kind, uint64_t heroId);
void UITwoColumns(std::function<void()> left, std::function<void()> right);
void SettingRow(const char* label, float labelWidthPx = kDefaultLabelWidth);
bool UIActionSlotEnabledCheckbox(ActionSlotKind kind,
                                        const HeroOption& hero,
                                        const char* checkboxId,
                                        float labelWidthPx);
bool UIColorEdit(const char* label, ImVec4* value,
                        float labelWidthPx = kDefaultLabelWidth);
void DrawCheckboxGrid3(const char* labels[], bool* values[], int rowCount,
                              float gapX, const float ratios[3]);
void DrawRuntimeReadinessDetails();
void DrawThemeGeneralPage();
void DrawThemeFovRingsPage();
void DrawMiscGeneralPage();
void DrawMiscDiagnosticsPage();
void DrawMiscBehaviorPage();
void DrawMiscMethodPage();
void DrawMiscDynamicFovPage();
void DrawMiscOutputPage();
void DrawMiscScreenPage();
