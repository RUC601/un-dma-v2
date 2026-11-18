#define NOMINMAX
#include "ui/vision/ContourPanel.hpp"
#include "ui/EditorSupport.hpp"
#include "ui/ColorPicker.hpp"
#include "config/ContourSettings.hpp"
#include "vision/ContourService.hpp"
#include "vision/ContourDetection.hpp"
#include "Renderer/Overlay.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <vector>

namespace UI::Vision {
namespace {
using namespace V2::Vision;
#ifdef UNLEASHED_ASSET_PREVIEW
ID3D11Device* previewDevice=nullptr;
ID3D11DeviceContext* previewContext=nullptr;
#endif
struct Texture {
    ID3D11Texture2D* texture=nullptr;
    ID3D11ShaderResourceView* view=nullptr;
    int width=0,height=0,mode=-1;
    std::uint64_t epoch=0,sequence=0;
    void Reset(){if(view)view->Release();if(texture)texture->Release();view=nullptr;texture=nullptr;width=height=0;sequence=epoch=0;mode=-1;}
} texture;
std::string Utf8(const std::wstring& value) {
    if(value.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);return result;
}
const char* Tr(const char* en,const char* zh){return OW::Config::uiLanguage==1?zh:en;}
bool Upload(const Frame& frame,int mode) {
    auto* device=g_Overlay.GetD3DDevice();auto* context=g_Overlay.GetD3DContext();
#ifdef UNLEASHED_ASSET_PREVIEW
    device=previewDevice;context=previewContext;
#endif
    if(!device||!context||frame.width<=0||frame.height<=0||frame.pixels.size()!=size_t(frame.width)*frame.height*4)return false;
    if(texture.width!=frame.width||texture.height!=frame.height){
        texture.Reset();D3D11_TEXTURE2D_DESC desc{};desc.Width=frame.width;desc.Height=frame.height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture.texture))||FAILED(device->CreateShaderResourceView(texture.texture,nullptr,&texture.view))){texture.Reset();return false;}
        texture.width=frame.width;texture.height=frame.height;
    }
    if(texture.epoch==frame.epoch&&texture.sequence==frame.sequence&&texture.mode==mode)return true;
    const std::uint8_t* data=frame.pixels.data();std::vector<std::uint8_t> mask;
    if(mode==1){
        mask.assign(frame.pixels.size(),0);
        for(size_t i=0;i<mask.size()/4;++i){const auto bits=i<frame.mask.size()?frame.mask[i]:0;
            if(bits){mask[i*4]=frame.pixels[i*4];mask[i*4+1]=frame.pixels[i*4+1];mask[i*4+2]=frame.pixels[i*4+2];}mask[i*4+3]=255;}
        data=mask.data();
    }
    context->UpdateSubresource(texture.texture,0,nullptr,data,static_cast<UINT>(frame.width*4),0);
    texture.epoch=frame.epoch;texture.sequence=frame.sequence;texture.mode=mode;return true;
}
struct Enumeration {std::vector<CaptureCardDevice> devices;std::wstring error;bool ok=false;};
std::future<Enumeration> enumeration;
std::vector<CaptureCardDevice> devices;
std::shared_ptr<const Frame> preview;
bool previewFrozen=false;
void DrawPreview(Settings& draft,bool& edited) {
    auto& frozen=previewFrozen;static int mode=0,colorSlot=1,pickX=0,pickY=0;
    static Rgb picked{};static bool pickedValid=false;
    const char* modes[]={Tr("Original","原始画面"),Tr("Match mask","匹配掩码"),Tr("Original + contours","原图与轮廓")};
    SettingRow(Tr("Preview","预览"));PushControlWidth();UISelect("##contourPreviewMode",&mode,modes,3,false);ImGui::PopItemWidth();
    SettingRow(Tr("Freeze for sampling","定格取色"));UICheckbox("##contourFreeze",&frozen);
    if(!frozen)preview=GetService().Snapshot();
    if(!preview){
        ImGui::TextWrapped("%s",Tr("Start capture to see the raw frame. A missing or stale frame cannot authorize firing.","开始采集后显示原始画面。无画面或画面过期时，轮廓路径不允许开火。"));return;
    }
    const auto frame=preview;
    if(!Upload(*frame,mode)) {ImGui::TextWrapped("%s",Tr("Preview unavailable. Stop and restart capture, then reopen this page.","预览不可用。请停止并重新开始采集，再打开此页。"));return;}
    const float available=(std::max)(80.f,ImGui::GetContentRegionAvail().x);
    const float scale=(std::min)(available/frame->width,440.f/frame->height);
    const ImVec2 size(frame->width*scale,frame->height*scale),start=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##captureImage",size);
    auto* draw=ImGui::GetWindowDrawList();const ImVec2 end(start.x+size.x,start.y+size.y);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture.view),start,end);
    draw->AddRect(ImVec2(start.x+1,start.y+1),ImVec2(end.x-1,end.y-1),IM_COL32(255,255,255,26),0.f,1.f);
    if(mode==2){
        auto point=[&](float x,float y){return ImVec2(start.x+x*scale,start.y+y*scale);};
        const auto& roi=frame->detectionRoi;draw->AddRect(point(float(roi.left),float(roi.top)),point(float(roi.right),float(roi.bottom)),IM_COL32(100,210,255,210));
        for(const auto& c:frame->components){const auto& b=c.bounds;draw->AddRect(point(float(b.left),float(b.top)),point(float(b.right),float(b.bottom)),IM_COL32(255,210,70,210));}
    }
    if(ImGui::IsItemHovered()){
        const auto mouse=ImGui::GetIO().MousePos;
        const int x=std::clamp(int((mouse.x-start.x)/scale),0,frame->width-1),y=std::clamp(int((mouse.y-start.y)/scale),0,frame->height-1);
        Rgb color;if(SampleColor(*frame,x,y,color)){
            ImGui::SetTooltip("%d, %d  RGB %u %u %u  #%02X%02X%02X",x,y,color.r,color.g,color.b,color.r,color.g,color.b);
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){pickX=x;pickY=y;picked=color;pickedValid=true;frozen=true;}
        }
    }
    if(ImGui::IsItemFocused())draw->AddRect(start,end,ImGui::GetColorU32(ImGuiCol_NavCursor),0.f,2.f);
    ImGui::Text(Tr("%d x %d | frame %llu | %zu contours | age %.0f ms%s","%d × %d | 帧 %llu | %zu 个轮廓 | 帧龄 %.0f ms%s"),frame->width,frame->height,
        static_cast<unsigned long long>(frame->sequence),frame->components.size(),(std::max)(0.0,MonotonicMs()-frame->arrivalMs),frozen?Tr(" | frozen"," | 已定格"):"");
    ImGui::TextWrapped("%s",Tr("Click the image to sample the original pixel. You can also enter X/Y and press Sample; preview scaling and annotations never affect the sampled color.","点击画面可取原始像素颜色，也可填写 X/Y 后点击取色；预览缩放和标记不影响取色。"));
    ImGui::SetNextItemWidth(100);ImGui::InputInt("X##sampleX",&pickX,0);ImGui::SameLine();ImGui::SetNextItemWidth(100);ImGui::InputInt("Y##sampleY",&pickY,0);
    ImGui::SameLine();if(ImGui::Button(Tr("Sample","取色"))){pickedValid=SampleColor(*frame,pickX,pickY,picked);frozen=true;}
    const char* slots[]={Tr("Hidden color","不可见颜色"),Tr("Visible color","可见颜色"),Tr("Selected color","选中颜色")};
    SettingRow(Tr("Sample destination","取色写入"));PushControlWidth();UISelect("##sampleSlot",&colorSlot,slots,3,false);ImGui::PopItemWidth();
    if(pickedValid){
        ImGui::ColorButton("##pickedColor",ImVec4(picked.r/255.f,picked.g/255.f,picked.b/255.f,1));ImGui::SameLine();
        ImGui::Text("#%02X%02X%02X  RGB %u %u %u",picked.r,picked.g,picked.b,picked.r,picked.g,picked.b);
        if(ImGui::Button(Tr("Use sampled color","使用此颜色"))){draft.useOutlineColors=false;draft.colors[colorSlot]=picked;edited=true;}
        ImGui::SetItemTooltip("%s",Tr("Updates the detection color draft only. Click Apply settings below to use it.","仅更新检测颜色草稿；点击下方应用设置后生效。"));
    }
}
}
void ReleasePreviewResources(){texture.Reset();preview.reset();previewFrozen=false;}
#ifdef UNLEASHED_ASSET_PREVIEW
void SetPreviewDevice(ID3D11Device* device,ID3D11DeviceContext* context){ReleasePreviewResources();previewDevice=device;previewContext=context;}
#endif
void DrawPage() {
    using namespace V2::Vision;
    if(!V2::Vision::Preferences::Loaded())V2::Vision::Preferences::Load();
    static Settings draft=GetService().GetSettings();static bool edited=false;static std::string feedback;
    auto current=GetService().GetSettings();const auto status=GetService().Status();
    const bool active=status.state==ServiceState::Running||status.state==ServiceState::Starting;
    if(!edited)draft=current;
    ImGui::PushID("ContourCapture");UIGroupBox(Tr("Capture card and contours","采集卡与轮廓"));
    const char* state=status.state==ServiceState::Running?Tr("Running","采集中"):status.state==ServiceState::Starting?Tr("Starting","正在连接"):
        status.state==ServiceState::Faulted?Tr("Capture failed","采集失败"):Tr("Stopped","已停止");
    ImGui::Text("%s",state);
    if(active)ImGui::Text(Tr("%d x %d  %.1f fps | received %llu / processed %llu / dropped %llu","%d × %d  %.1f fps | 接收 %llu / 处理 %llu / 丢弃 %llu"),
        status.width,status.height,status.fps,static_cast<unsigned long long>(status.receivedFrames),static_cast<unsigned long long>(status.processedFrames),static_cast<unsigned long long>(status.droppedFrames));
    if(!status.error.empty()){ImGui::TextWrapped("%s",Utf8(status.error).c_str());ImGui::TextWrapped("%s",Tr("Check the device and selected format, then try Start capture again.","请检查设备与所选格式，再点击开始采集重试。"));}
    if(enumeration.valid()&&enumeration.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto result=enumeration.get();devices=std::move(result.devices);feedback=result.ok?Tr("Device list refreshed.","设备列表已刷新。"):Utf8(result.error);}
    const bool enumerating=enumeration.valid();
    SettingRow(Tr("Capture device","采集设备"));PushControlWidth();
    const auto selectedName=Utf8(draft.capture.deviceName);
    if(ImGui::BeginCombo("##captureDevice",selectedName.empty()?Tr("Select device","选择设备"):selectedName.c_str())){
        for(const auto& device:devices){const auto name=Utf8(device.friendlyName);const bool selected=draft.capture.devicePath==device.devicePath;
            ImGui::PushID(Utf8(device.devicePath).c_str());if(ImGui::Selectable(name.c_str(),selected)){draft.capture.devicePath=device.devicePath;draft.capture.deviceName=device.friendlyName;edited=true;}ImGui::PopID();}
        if(devices.empty())ImGui::TextUnformatted(Tr("Refresh the device list first.","请先刷新设备列表。"));ImGui::EndCombo();}
    ImGui::PopItemWidth();
    ImGui::BeginDisabled(enumerating);if(ImGui::Button(enumerating?Tr("Finding devices...","正在查找设备…"):Tr("Refresh devices","刷新设备")))
        enumeration=std::async(std::launch::async,[]{Enumeration result;result.ok=Service::EnumerateDevices(result.devices,result.error);return result;});ImGui::EndDisabled();
    auto intRow=[&](const char* en,const char* zh,const char* id,int& value){SettingRow(Tr(en,zh));PushControlWidth();edited|=ImGui::InputInt(id,&value,0,0);ImGui::PopItemWidth();};
    auto floatRow=[&](const char* en,const char* zh,const char* id,float& value){SettingRow(Tr(en,zh));PushControlWidth();edited|=ImGui::InputFloat(id,&value,0,0,"%.1f");ImGui::PopItemWidth();};
    intRow("Capture width","采集宽度","##captureWidth",draft.capture.width);intRow("Capture height","采集高度","##captureHeight",draft.capture.height);
    SettingRow(Tr("Frame rate","帧率"));PushControlWidth();edited|=ImGui::InputDouble("##captureFps",&draft.capture.fps,0,0,"%.3f");ImGui::PopItemWidth();
    SettingRow(Tr("Use game outline colors","沿用游戏描边颜色"));edited|=UICheckbox("##useOutlineColors",&draft.useOutlineColors);
    if(draft.useOutlineColors){const auto palette=GetService().GetSettings().colors;draft.colors=palette;}
    const char* colorNames[]={Tr("Hidden","不可见"),Tr("Visible","可见"),Tr("Selected","选中")};
    for(int i=0;i<3;++i){ImGui::PushID(i);SettingRow(colorNames[i]);auto& c=draft.colors[i];ImVec4 color(c.r/255.f,c.g/255.f,c.b/255.f,1);
        ImGui::BeginDisabled(draft.useOutlineColors);
        if(UI::ColorPicker("detectorColor",color)){c={std::uint8_t(std::lround(color.x*255)),std::uint8_t(std::lround(color.y*255)),std::uint8_t(std::lround(color.z*255))};edited=true;}
        ImGui::SameLine();if(ImGui::Button(Tr("Red","红色"))){c={255,0,0};edited=true;}ImGui::SameLine();if(ImGui::Button(Tr("Yellow","黄色"))){c={255,255,0};edited=true;}
        ImGui::EndDisabled();ImGui::SameLine();ImGui::Text("#%02X%02X%02X",c.r,c.g,c.b);ImGui::PopID();}
    ImGui::TextWrapped("%s",Tr("All three colors are matched. A color identifies a contour fragment, not a player.","三种颜色均参与检测，颜色本身不代表某个玩家身份。"));
    intRow("RGB tolerance (0-128)","RGB 容差（0–128）","##contourTolerance",draft.tolerance);
    intRow("Search width (px)","搜索宽度（像素）","##contourRoiWidth",draft.roiWidth);intRow("Search height (px)","搜索高度（像素）","##contourRoiHeight",draft.roiHeight);
    if(ImGui::CollapsingHeader(Tr("Association and firing","关联与开火"))){
        SettingRow(Tr("Maximum frame age (ms)","最大帧龄（毫秒）"));PushControlWidth();edited|=ImGui::InputDouble("##contourAge",&draft.maxFrameAgeMs,0,0,"%.0f");ImGui::PopItemWidth();
        floatRow("Association radius (px)","关联半径（像素）","##contourAssociation",draft.associationRadiusPx);
        floatRow("Contact radius (px)","接触半径（像素）","##contourContact",draft.contactRadiusPx);
        intRow("Contact delay (ms)","触边延迟（毫秒）","##contourDelay",draft.touchDelayMs);intRow("Loss grace (ms)","丢失宽限（毫秒）","##contourGrace",draft.lostGraceMs);
        intRow("Minimum fragment pixels","最小片段像素数","##contourMinPixels",draft.minComponentPixels);
        ImGui::TextWrapped("%s",Tr("Delay begins at contact with the associated target. Loss grace preserves association only; stale frames never authorize firing.","延迟从接触当前目标轮廓开始。丢失宽限只保持关联；过期画面不允许开火。"));
    }
    DrawPreview(draft,edited);
    std::wstring validation;const bool valid=ValidateSettings(draft,&validation);
    if(!valid)ImGui::TextWrapped("%s",Utf8(validation).c_str());
    auto apply=[&](){draft.enabled=GetService().GetSettings().enabled;std::wstring error;if(!GetService().Configure(draft,&error)){feedback=Utf8(error);return false;}SyncOutlinePalette();draft=GetService().GetSettings();edited=false;return true;};
    ImGui::BeginDisabled(!valid);
    if(ImGui::Button(Tr("Apply settings","应用设置"))&&apply())feedback=Tr("Settings applied.","设置已应用。");
    ImGui::SameLine();if(ImGui::Button(Tr("Save settings","保存设置"))&&apply())feedback=V2::Vision::Preferences::Save()?Tr("Saved. Capture stays stopped on next launch.","已保存；下次启动仍为停止状态。"):V2::Vision::Preferences::Error();
    ImGui::BeginDisabled(active);if(ImGui::Button(Tr("Start capture","开始采集"))&&apply()){auto start=GetService().GetSettings();start.enabled=true;GetService().Configure(start);if(!GetService().Start())feedback=Tr("Could not start capture. Check the status above.","无法开始采集，请查看上方状态。");}ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(!active&&!current.enabled);if(ImGui::Button(Tr("Stop capture","停止采集"))){GetService().Stop();auto stopped=GetService().GetSettings();stopped.enabled=false;GetService().Configure(stopped);ReleasePreviewResources();}ImGui::EndDisabled();
    if(edited)ImGui::TextDisabled("%s",Tr("Changes have not been applied.","修改尚未应用。"));
    if(!feedback.empty())ImGui::TextWrapped("%s",feedback.c_str());
    if(!V2::Vision::Preferences::Error().empty())ImGui::TextWrapped("%s",V2::Vision::Preferences::Error().c_str());
    CloseGroupBox();ImGui::PopID();
}
}
