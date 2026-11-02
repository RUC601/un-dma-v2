#pragma once
namespace V2::Visuals {
// 渲染线程入口。每帧按需持有发布快照，绘制函数不创建采集/输出线程。
// 配置沿用渲染调用者的 ScopedConfigView，过期身体与相机规则在绘制前复核。
void DrawFrame();
}
