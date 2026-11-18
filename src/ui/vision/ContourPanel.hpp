#pragma once
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace UI::Vision {
void DrawPage();
// Render-thread only; call before releasing Overlay's D3D device.
void ReleasePreviewResources();
#ifdef UNLEASHED_ASSET_PREVIEW
void SetPreviewDevice(ID3D11Device* device, ID3D11DeviceContext* context);
#endif
}
