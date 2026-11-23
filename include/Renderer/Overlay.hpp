#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <functional>

#include "Renderer.hpp"

// -----------------------------------------------------------------------
// Overlay -- two-layer DX11 overlay:
// 1) a full-screen canvas for ESP draw lists and background clicks
// 2) a separate topmost borderless app window for the interactive ImGui menu
// -----------------------------------------------------------------------

class Overlay {
public:
    bool Initialize(const wchar_t* overlayTitle);
    void Run(std::function<void()> renderCallback);
    void MinimizeToTaskbar();
    void RestoreFromTaskbar();
    void ToggleMenu();
    void HideMenu();
    void OnMenuActivated();
    void OnMenuDeactivated(HWND activatedWindow);
    bool SetCanvasTransparentBackground(bool enabled);
    bool IsCanvasTransparentBackground() const { return m_canvasTransparentBackground; }
    void RequestExit();
    void Shutdown();

    HWND GetOverlayWindow() const { return m_canvasHWnd; }
    HWND GetMenuWindow() const { return m_menuHWnd; }
    // UI-owned preview textures are created and released on the render thread.
    ID3D11Device* GetD3DDevice() const { return m_pDevice; }
    ID3D11DeviceContext* GetD3DContext() const { return m_pContext; }

private:
    friend struct OverlayWindowSelfTestAccess;
    HWND                    m_canvasHWnd = nullptr;
    HWND                    m_menuHWnd = nullptr;
    ID3D11Device*           m_pDevice = nullptr;
    ID3D11DeviceContext*    m_pContext = nullptr;
    IDXGISwapChain*         m_canvasSwapChain = nullptr;
    IDXGISwapChain*         m_menuSwapChain = nullptr;
    ID3D11RenderTargetView* m_canvasRenderTargetView = nullptr;
    ID3D11RenderTargetView* m_menuRenderTargetView = nullptr;
    ImGuiContext*           m_canvasContext = nullptr;
    ImGuiContext*           m_menuContext = nullptr;
    int                     m_canvasX = 0;
    int                     m_canvasY = 0;
    UINT                    m_canvasWidth = 0;
    UINT                    m_canvasHeight = 0;
    UINT                    m_menuWidth = 0;
    UINT                    m_menuHeight = 0;
    UINT                    m_canvasSwapChainFlags = 0;
    UINT                    m_menuSwapChainFlags = 0;
    bool                    m_canvasAllowTearing = false;
    int                     m_menuAnchorX = 0;
    int                     m_menuAnchorY = 0;
    unsigned long long      m_lastCanvasBoundsRefreshMs = 0;
    unsigned long long      m_lastMenuRenderMs = 0;
    bool                    m_menuAnchorValid = false;
    bool                    m_minimizedToTaskbar = false;
    bool                    m_canvasTransparentBackground = false;
    bool                    m_uiResourcesInitialized = false;
    DWORD                   m_windowServicesThread = 0;

    bool InitializeWindowServices();
    void ShutdownWindowServices();
    bool RegisterWindowClasses(HINSTANCE instance);
    bool CreateWindows(const wchar_t* overlayTitle, HINSTANCE instance);
    bool CreateDX11();
    void CleanupDX11();
    void ApplyCanvasWindowStyle();
    bool RecreateCanvasSwapChain();
    bool CreateSwapChainForWindow(HWND hWnd, UINT width, UINT height, bool allowTearingPreferred,
                                  IDXGISwapChain** swapChain, UINT* flagsOut, bool* allowTearingOut,
                                  bool forceDiscard);
    bool CreateRenderTarget(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView);
    void ReleaseRenderTarget(ID3D11RenderTargetView** renderTargetView);
    bool ResizeSwapChain(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView,
                         UINT width, UINT height, UINT swapChainFlags);
    bool InitializeImGuiContext(ImGuiContext** context, HWND hWnd);
    void ShutdownImGuiContext(ImGuiContext*& context);
    void UpdateWindowVisibility();
    void SetWindowsTopmost(bool topmost);
    void UpdateCanvasBounds(bool force = false);
    void ApplyDesiredMenuClientSize();
    void UpdateSwapChainSizes();
    void RenderCanvas(std::function<void()> renderCallback);
    void RenderMenu();
};

// Convenience global overlay instance
inline Overlay g_Overlay;
