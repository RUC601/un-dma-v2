#include "runtime/LauncherBridge.hpp"
#include "ui/FontFile.hpp"
#include "Renderer/Overlay.hpp"
#include "Renderer/FrameSchedule.hpp"
#include "Features/UI.hpp"
#include "ui/vision/ContourPanel.hpp"
#include "config/UiPreferences.hpp"
#include "Game/HeroSettings.hpp"
#include <fstream>
#include <filesystem>
#include "Game/Offsets.hpp"
#include "Game/SDK.hpp"
#include "Game/Overwatch.hpp"
#include "Memory/KeyState.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "resource.h"

#include <algorithm>
#include <dcomp.h>
#include <dxgi1_5.h>
#include <windowsx.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr int kDefaultMenuClientWidth = 1160;
constexpr int kDefaultMenuClientHeight = 800;
constexpr int kMinimumMenuClientHeight = 480;
constexpr int kInitialMenuTopMargin = 24;
constexpr int kInitialMenuBottomMargin = 16;
constexpr int kMenuDragHeight = 41;
constexpr int kHeaderActionWidth = 364;
constexpr wchar_t kCanvasClassName[] = L"UNDMAOverlayCanvasDX11";
constexpr wchar_t kMenuClassName[] = L"UNDMAOverlayMenuDX11";
constexpr ULONGLONG kCanvasBoundsRefreshMs = 2000;
constexpr ULONGLONG kMenuRenderIntervalMs = 16;

ImGuiContext* g_menuContext = nullptr;

struct FrameWaitTimer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ~FrameWaitTimer() { if (handle) CloseHandle(handle); }

    void WaitUntil(V2::Rendering::FrameSchedule::Clock::time_point deadline) const {
        using Clock = V2::Rendering::FrameSchedule::Clock;
        while (Clock::now() < deadline) {
            const auto remaining = deadline - Clock::now();
            // Sleep most of the interval; a short tail avoids the 1 ms Sleep floor at 2000 FPS.
            if (handle && remaining > std::chrono::microseconds(150)) {
                LARGE_INTEGER due{};
                due.QuadPart = -(std::max)(1LL,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        remaining - std::chrono::microseconds(100)).count() / 100);
                if (SetWaitableTimer(handle, &due, 0, nullptr, nullptr, FALSE)) {
                    const DWORD result = MsgWaitForMultipleObjectsEx(
                        1, &handle, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    if (result == WAIT_OBJECT_0 + 1) return;
                    if (result == WAIT_OBJECT_0) continue;
                }
            }
            if (remaining > std::chrono::milliseconds(2)) {
                const DWORD timeout = static_cast<DWORD>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count() - 1);
                if (MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT,
                        MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0) return;
            } else {
                MSG message;
                if (PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE)) return;
                YieldProcessor();
            }
        }
    }
};

struct CanvasBounds {
    int x = 0;
    int y = 0;
    UINT width = 1;
    UINT height = 1;
};

struct MonitorBoundsSearch {
    RECT rect = {};
    long long bestArea = 0;
};

int AbsInt(int value) {
    return value < 0 ? -value : value;
}

int MaxInt(int a, int b) {
    return a > b ? a : b;
}

int MinInt(int a, int b) {
    return a < b ? a : b;
}

int RoundClientSize(float value) {
    return static_cast<int>(value + 0.5f);
}

bool TryMakeCanvasBounds(const RECT& rect, CanvasBounds& bounds) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return false;

    bounds.x = rect.left;
    bounds.y = rect.top;
    bounds.width = static_cast<UINT>(width);
    bounds.height = static_cast<UINT>(height);
    return true;
}

bool QueryAllowTearingSupport(IDXGIFactory* factory) {
    if (!factory)
        return false;
    IDXGIFactory5* factory5 = nullptr;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory5))) || !factory5)
        return false;
    BOOL allowTearing = FALSE;
    const HRESULT hr = factory5->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    factory5->Release();
    return SUCCEEDED(hr) && allowTearing != FALSE;
}

bool TryGetMonitorBounds(HMONITOR monitor, CanvasBounds& bounds) {
    if (!monitor)
        return false;

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return false;

    return TryMakeCanvasBounds(monitorInfo.rcMonitor, bounds);
}

bool TryGetPrimaryMonitorBounds(CanvasBounds& bounds) {
    POINT origin = { 0, 0 };
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    return TryGetMonitorBounds(monitor, bounds);
}

BOOL CALLBACK EnumLargestMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM lParam) {
    MonitorBoundsSearch* search = reinterpret_cast<MonitorBoundsSearch*>(lParam);
    if (!search)
        return TRUE;

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return TRUE;

    const RECT rect = monitorInfo.rcMonitor;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const long long area = static_cast<long long>(width) * static_cast<long long>(height);
    if (width <= 0 || height <= 0 || area <= 0)
        return TRUE;

    if (area > search->bestArea) {
        search->rect = rect;
        search->bestArea = area;
    }

    return TRUE;
}

bool TryGetLargestMonitorBounds(CanvasBounds& bounds) {
    MonitorBoundsSearch search = {};
    EnumDisplayMonitors(nullptr, nullptr, EnumLargestMonitor, reinterpret_cast<LPARAM>(&search));
    if (search.bestArea <= 0)
        return false;

    return TryMakeCanvasBounds(search.rect, bounds);
}

CanvasBounds ResolveCanvasBounds() {
    CanvasBounds bounds = {};
    if (TryGetPrimaryMonitorBounds(bounds))
        return bounds;
    if (TryGetLargestMonitorBounds(bounds))
        return bounds;

    bounds.width = static_cast<UINT>(MaxInt(1, GetSystemMetrics(SM_CXSCREEN)));
    bounds.height = static_cast<UINT>(MaxInt(1, GetSystemMetrics(SM_CYSCREEN)));
    return bounds;
}

void ApplyMinimumMenuTrackSize(HWND window, LPARAM lParam) {
    MINMAXINFO* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
    if (!minMax)
        return;

    const float dpiScale = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
    minMax->ptMinTrackSize.x = static_cast<LONG>(720 * dpiScale);
    minMax->ptMinTrackSize.y = static_cast<LONG>(kMinimumMenuClientHeight * dpiScale);
}

bool IsInMenuDragZone(HWND hWnd, POINT cursor) {
    RECT rect = {};
    GetClientRect(hWnd, &rect);

    if (cursor.x < 0 || cursor.y < 0 || cursor.x >= rect.right || cursor.y >= rect.bottom)
        return false;

    if (cursor.y >= kMenuDragHeight)
        return false;

    if (cursor.x >= rect.right - kHeaderActionWidth)
        return false;

    return true;
}

LRESULT HitTestBorderlessMenu(HWND hWnd, LPARAM lParam) {
    POINT cursor = {
        GET_X_LPARAM(lParam),
        GET_Y_LPARAM(lParam)
    };
    ScreenToClient(hWnd, &cursor);

    RECT bounds{};
    GetClientRect(hWnd,&bounds);
    const int edge=6;
    const bool left=cursor.x<edge,right=cursor.x>=bounds.right-edge;
    const bool top=cursor.y<edge,bottom=cursor.y>=bounds.bottom-edge;
    if(top&&left)return HTTOPLEFT;
    if(top&&right)return HTTOPRIGHT;
    if(bottom&&left)return HTBOTTOMLEFT;
    if(bottom&&right)return HTBOTTOMRIGHT;
    if(left)return HTLEFT;
    if(right)return HTRIGHT;
    if(top)return HTTOP;
    if(bottom)return HTBOTTOM;
    if (IsInMenuDragZone(hWnd, cursor))
        return HTCAPTION;

    return HTCLIENT;
}

bool IsMenuTogglePressed() {
    const int key = OW::Config::MenuToggleKey;
    static int previousKey = 0;
    static bool localWasDown = false;
    static bool remoteWasDown = false;
    const bool bindingChanged = key != previousKey;
    if (bindingChanged) {
        previousKey = key;
        localWasDown = false;
        remoteWasDown = false;
    }
    if (key <= 0 || key >= 256)
        return false;

    const bool localDown = (GetAsyncKeyState(key) & 0x8000) != 0;
    const auto remote = KeyState::SampleVk(key);
    const bool wasDown = localWasDown || (remote.available && remoteWasDown);
    localWasDown = localDown;
    // Missing remote samples preserve its edge state without blocking local input.
    if (remote.available)
        remoteWasDown = remote.down;
    const bool down = localDown || (remote.available && remote.down);
    return !bindingChanged && down && !wasDown;
}

bool IsWindowVisibleAndRestored(HWND hWnd) {
    return hWnd && IsWindow(hWnd) && IsWindowVisible(hWnd) && !IsIconic(hWnd);
}

void ConfigureCanvasFont(ImGuiIO& io) {
    if (io.Fonts->Locked)
        return;

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;
    fontConfig.RasterizerMultiply = 1.0f;

    const auto settings=V2::HeroSettings::Snapshot();
    const auto& display=settings->display;
    UI::OverlayFonts::error.clear();
    auto add=[&](const std::string& path,const std::string& fallback,ImFontConfig& config,const ImWchar* ranges=nullptr){
        auto bytes=UI::OverlayFonts::Read(path);
        if(bytes.empty()) {UI::OverlayFonts::error+="Font unavailable: "+path+"; ";bytes=UI::OverlayFonts::Read(fallback);}
        if(bytes.empty())return static_cast<ImFont*>(nullptr);
        void* owned=ImGui::MemAlloc(bytes.size());std::memcpy(owned,bytes.data(),bytes.size());
        return io.Fonts->AddFontFromMemoryTTF(owned,static_cast<int>(bytes.size()),14,&config,ranges);
    };
    ImFont* font=add(display.font,"C:/Windows/Fonts/consola.ttf",fontConfig);
    if(!font)font=io.Fonts->AddFontDefault();
    if(font)io.FontDefault=font;
    ImFontConfig fallback;fallback.MergeMode=true;
    add(display.chineseFont,"C:/Windows/Fonts/msyh.ttc",fallback,io.Fonts->GetGlyphRangesChineseFull());

}

DXGI_SWAP_CHAIN_DESC MakeSwapChainDesc(HWND hWnd, UINT width, UINT height, bool flipModel, bool allowTearing) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 0;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hWnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    desc.SwapEffect = flipModel ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
    desc.Flags = (flipModel && allowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    return desc;
}

bool RegisterClassIfNeeded(const WNDCLASSEXW& windowClass) {
    if (RegisterClassExW(&windowClass) != 0)
        return true;

    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT WINAPI CanvasWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            return g_Overlay.IsCanvasTransparentBackground() ? HTTRANSPARENT : HTCLIENT;
        case WM_MOUSEACTIVATE:
            if (g_Overlay.IsCanvasTransparentBackground())
                return MA_NOACTIVATE;
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE && HIWORD(wParam) == 0)
                g_Overlay.OnMenuActivated();
            else if (LOWORD(wParam) == WA_INACTIVE)
                g_Overlay.OnMenuDeactivated(reinterpret_cast<HWND>(lParam));
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            if (hWnd == g_Overlay.GetOverlayWindow() && !g_Overlay.IsRecreatingCanvasWindow())
                PostQuitMessage(0);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WINAPI MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool dragging = false;
    static POINT dragCursor{};
    static RECT dragWindow{};
    // Keep rendering during a title drag and avoid the OS outline-only move preview.
    if (msg == WM_NCLBUTTONDOWN && wParam == HTCAPTION) {
        GetCursorPos(&dragCursor);
        GetWindowRect(hWnd, &dragWindow);
        SetForegroundWindow(hWnd);
        SetCapture(hWnd);
        dragging = GetCapture() == hWnd;
        return 0;
    }
    if (msg == WM_MOUSEMOVE && dragging) {
        POINT cursor{};
        GetCursorPos(&cursor);
        SetWindowPos(hWnd, nullptr, dragWindow.left + cursor.x - dragCursor.x,
            dragWindow.top + cursor.y - dragCursor.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (msg == WM_LBUTTONUP && dragging) {
        dragging = false;
        ReleaseCapture();
        return 0;
    }
    if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) {
        dragging = false;
        if (msg == WM_CANCELMODE && GetCapture() == hWnd) ReleaseCapture();
    }
    if (msg == WM_NCPAINT || msg == WM_NCACTIVATE)
        return msg == WM_NCACTIVATE ? TRUE : 0;
    if (msg == WM_NCHITTEST)
        return HitTestBorderlessMenu(hWnd, lParam);
    if (msg == WM_NCCALCSIZE && wParam)
        return 0;
    if (msg == WM_LBUTTONDOWN) {
        POINT cursor = {
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };
        if (IsInMenuDragZone(hWnd, cursor)) {
            ReleaseCapture();
            SendMessageW(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
    }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    if (g_menuContext) {
        ImGui::SetCurrentContext(g_menuContext);
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            ImGui::SetCurrentContext(previousContext);
            return true;
        }
        ImGui::SetCurrentContext(previousContext);
    }

    switch (msg) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE && HIWORD(wParam) == 0)
                g_Overlay.OnMenuActivated();
            else if (LOWORD(wParam) == WA_INACTIVE)
                g_Overlay.OnMenuDeactivated(reinterpret_cast<HWND>(lParam));
            break;
        case WM_DPICHANGED: {
            const auto* rect=reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hWnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
            return 0;
        }
        case WM_EXITSIZEMOVE: {
            RECT client{};
            if(GetClientRect(hWnd,&client)) {
                const float dpi=GetDpiForWindow(hWnd)/96.f;
                UI::Preferences::values.windowWidth=int(client.right/dpi+.5f);
                UI::Preferences::values.windowHeight=int(client.bottom/dpi+.5f);
                UI::Preferences::Save();
            }
            return 0;
        }
        case WM_GETMINMAXINFO:
            ApplyMinimumMenuTrackSize(hWnd, lParam);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;
            if ((wParam & 0xFFF0) == SC_RESTORE)
                g_Overlay.RestoreFromTaskbar();
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                g_Overlay.MinimizeToTaskbar();
                return 0;
            }
            break;
        case WM_SIZE:
            if ((wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) &&
                IsWindowVisibleAndRestored(hWnd))
                g_Overlay.RestoreFromTaskbar();
            return 0;
        case WM_CLOSE:
            g_Overlay.HideMenu();
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

bool Overlay::Initialize(const wchar_t* overlayTitle) {
    if (!InitializeWindowServices())
        return false;
    const auto fail = [this]() { Shutdown(); return false; };
    ImGui_ImplWin32_EnableDpiAwareness();
    m_overlayTitle = overlayTitle ? overlayTitle : L"";
    m_canvasTransparentBackground = OW::Config::overlayTransparentBackground;
    m_desiredCanvasTransparentBackground = m_canvasTransparentBackground;
    m_canvasModeChangePending = false;
    m_inFrame = false;
    m_recreatingCanvasWindow = false;
    m_forceCanvasRender = false;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!RegisterWindowClasses(instance))
        return fail();

    if (!CreateWindows(overlayTitle, instance))
        return fail();

    if (!CreateDX11())
        return fail();

    IMGUI_CHECKVERSION();
    if (!InitializeImGuiContext(&m_canvasContext, m_canvasHWnd))
        return fail();
    if (!InitializeImGuiContext(&m_menuContext, m_menuHWnd))
        return fail();

    g_menuContext = m_menuContext;

    Render::Initialize(m_pDevice, m_pContext);
    Render::InitIcons(m_pDevice);
    UI::InitializeResources(m_pDevice);
    m_uiResourcesInitialized = true;

    UpdateWindowVisibility();
    return true;
}

void Overlay::Run(std::function<void()> renderCallback) {
    if (!m_canvasHWnd || !m_canvasSwapChain || !m_canvasRenderTargetView)
        return;

    V2::Rendering::FrameSchedule canvasSchedule;
    FrameWaitTimer frameTimer;
    bool running = true;
    while (running) {
        V2::Launcher::Poll();
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running)
            break;

        if (!IsWindow(m_canvasHWnd))
            break;

        // 【维护约束】恢复热键必须在“已最小化”的判断之前处理。
        // 不得加 !m_minimizedToTaskbar 条件，否则用户收起后就无法再用 Home 找回菜单。
        if (IsMenuTogglePressed())
            ToggleMenu();

        static OW::ProcessConnection::CacheRefreshHotkey cacheRefreshHotkey;
        const int refreshKey = OW::Config::CacheRefreshKey;
        const auto remoteKey = KeyState::SampleVk(refreshKey);
        const bool localKeyDown = !KeyState::IsDmaKeyStateReady() &&
            refreshKey > 0 && refreshKey < 256 &&
            (GetAsyncKeyState(refreshKey) & 0x8000) != 0;
        if (cacheRefreshHotkey.Update(refreshKey, localKeyDown, remoteKey.available,
                remoteKey.down, OW::ProcessConnection::IsRecoveryBusy()))
            OW::ProcessConnection::RequestCacheRefresh();

        UI::AutoSave();
        if (!m_canvasModeChangePending &&
            OW::Config::overlayTransparentBackground != m_canvasTransparentBackground) {
            m_desiredCanvasTransparentBackground = OW::Config::overlayTransparentBackground;
            m_canvasModeChangePending = true;
        }
        if (m_canvasModeChangePending)
            ApplyPendingCanvasModeChange();
        UpdateWindowVisibility();
        // The canvas cannot activate, so also yield when Home has hidden the menu.
        OnMenuDeactivated(GetForegroundWindow());
        if (m_minimizedToTaskbar) {
            // 整层收起时暂停绘制和尺寸刷新，真正把屏幕让给 Codex；继续处理窗口消息并轮询恢复热键。
            // 此时 render_fps 降低是预期行为；后台线程独立运行，不要为了 FPS 取消这里的保护。
            m_lastMenuRenderMs = 0;
            MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        UpdateCanvasBounds();
        UpdateSwapChainSizes();

        using Clock = V2::Rendering::FrameSchedule::Clock;
        const auto now = Clock::now();
        canvasSchedule.SetLimit(OW::Config::overlayFrameLimit, now);
        if (m_forceCanvasRender || canvasSchedule.Due(now)) {
            m_forceCanvasRender = false;
            canvasSchedule.FrameStarted(now);
            RenderCanvas(renderCallback);
        }
        if (OW::Config::Menu) {
            const ULONGLONG now = GetTickCount64();
            if (m_lastMenuRenderMs == 0 ||
                now - m_lastMenuRenderMs >= kMenuRenderIntervalMs) {
                m_lastMenuRenderMs = now;
                RenderMenu();
            }
        } else {
            m_lastMenuRenderMs = 0;
        }
        if (m_canvasModeChangePending) {
            ApplyPendingCanvasModeChange();
            continue;
        }
        if (OW::Config::overlayFrameLimit > 0) {
            // Keep menu, autosave and Home polling responsive even at a very low canvas FPS.
            auto wake = (std::min)(canvasSchedule.Next(), Clock::now() + std::chrono::milliseconds(8));
            if (OW::Config::Menu && m_lastMenuRenderMs) {
                const auto elapsed = GetTickCount64() - m_lastMenuRenderMs;
                const auto menuWait = elapsed < kMenuRenderIntervalMs ? kMenuRenderIntervalMs - elapsed : 0;
                wake = (std::min)(wake, Clock::now() + std::chrono::milliseconds(menuWait));
            }
            frameTimer.WaitUntil(wake);
        }
    }

    Shutdown();
}

void Overlay::RequestExit() {
    if (m_canvasHWnd && IsWindow(m_canvasHWnd)) {
        PostMessageW(m_canvasHWnd, WM_CLOSE, 0, 0);
        return;
    }

    PostQuitMessage(0);
}

void Overlay::Shutdown() {
    m_canvasModeChangePending = false;
    m_inFrame = false;
    m_recreatingCanvasWindow = false;
    m_forceCanvasRender = false;
    UI::Vision::ReleasePreviewResources();
    if (m_uiResourcesInitialized) {
        UI::ShutdownResources();
        m_uiResourcesInitialized = false;
    }
    Render::ShutdownIcons();

    g_menuContext = nullptr;
    ShutdownImGuiContext(m_menuContext);
    ShutdownImGuiContext(m_canvasContext);

    CleanupDX11();

    if (m_menuHWnd) {
        DestroyWindow(m_menuHWnd);
        m_menuHWnd = nullptr;
    }
    if (m_canvasHWnd) {
        DestroyWindow(m_canvasHWnd);
        m_canvasHWnd = nullptr;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    UnregisterClassW(kMenuClassName, instance);
    UnregisterClassW(kCanvasClassName, instance);
    ShutdownWindowServices();
}

bool Overlay::RegisterWindowClasses(HINSTANCE instance) {
    WNDCLASSEXW canvasClass = {};
    canvasClass.cbSize = sizeof(WNDCLASSEXW);
    canvasClass.style = CS_HREDRAW | CS_VREDRAW;
    canvasClass.lpfnWndProc = CanvasWndProc;
    canvasClass.hInstance = instance;
    canvasClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    canvasClass.hbrBackground = nullptr;
    canvasClass.lpszClassName = kCanvasClassName;
    canvasClass.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_UNLEASHED));
    canvasClass.hIconSm = LoadIcon(instance, MAKEINTRESOURCE(IDI_UNLEASHED));

    WNDCLASSEXW menuClass = canvasClass;
    menuClass.lpfnWndProc = MenuWndProc;
    menuClass.lpszClassName = kMenuClassName;

    return RegisterClassIfNeeded(canvasClass) && RegisterClassIfNeeded(menuClass);
}

HWND Overlay::CreateCanvasWindow(bool transparentBackground, HINSTANCE instance) const {
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (transparentBackground)
        exStyle |= WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;

    return CreateWindowExW(
        exStyle,
        kCanvasClassName,
        m_overlayTitle.c_str(),
        WS_POPUP,
        m_canvasX,
        m_canvasY,
        static_cast<int>(m_canvasWidth),
        static_cast<int>(m_canvasHeight),
        nullptr,
        nullptr,
        instance,
        nullptr
    );
}

bool Overlay::CreateWindows(const wchar_t* overlayTitle, HINSTANCE instance) {
    m_overlayTitle = overlayTitle ? overlayTitle : L"";
    const CanvasBounds initialBounds = ResolveCanvasBounds();
    m_canvasX = initialBounds.x;
    m_canvasY = initialBounds.y;
    m_canvasWidth = initialBounds.width;
    m_canvasHeight = initialBounds.height;
    OW::RefreshScreenSizeFromConfig();

    m_canvasHWnd = CreateCanvasWindow(m_canvasTransparentBackground, instance);
    if (!m_canvasHWnd)
        return false;

    ShowWindow(m_canvasHWnd, m_canvasTransparentBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    SetWindowPos(m_canvasHWnd, HWND_TOPMOST, m_canvasX, m_canvasY,
                 static_cast<int>(m_canvasWidth), static_cast<int>(m_canvasHeight),
                 SWP_SHOWWINDOW | (m_canvasTransparentBackground ? SWP_NOACTIVATE : 0));

    const DWORD menuStyle = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
    const DWORD menuExStyle = WS_EX_TOPMOST | WS_EX_APPWINDOW;
    UI::Preferences::Load(OW::Config::uiLanguage);
    const float dpi=GetDpiForSystem()/96.f;
    const int menuW=int(UI::Preferences::values.windowWidth*dpi+.5f);
    const int menuH=int(UI::Preferences::values.windowHeight*dpi+.5f);

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workW = workArea.right - workArea.left;
    const int workH = workArea.bottom - workArea.top;
    const int menuX = workArea.left + (workW - menuW) / 2;
    const int desiredMenuY = workArea.top + kInitialMenuTopMargin;
    const int minMenuY = workArea.top;
    const int maxMenuY = MaxInt(minMenuY, workArea.bottom - menuH - kInitialMenuBottomMargin);
    const int menuY = MaxInt(minMenuY, MinInt(desiredMenuY, maxMenuY));

    m_menuHWnd = CreateWindowExW(
        menuExStyle,
        kMenuClassName,
        L"UNLEASHED V2",
        menuStyle,
        menuX,
        menuY,
        MinInt(menuW,workW),
        MinInt(menuH,workH),
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (!m_menuHWnd)
        return false;

    RECT clientRect = {};
    if (GetClientRect(m_menuHWnd, &clientRect)) {
        m_menuWidth = static_cast<UINT>(clientRect.right - clientRect.left);
        m_menuHeight = static_cast<UINT>(clientRect.bottom - clientRect.top);
    } else {
        m_menuWidth = kDefaultMenuClientWidth;
        m_menuHeight = kDefaultMenuClientHeight;
    }

    SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

bool Overlay::CreateDX11() {
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL featureLevelArray[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevelArray,
        ARRAYSIZE(featureLevelArray),
        D3D11_SDK_VERSION,
        &m_pDevice,
        &featureLevel,
        &m_pContext
    );
    if (FAILED(hr) || !m_pDevice || !m_pContext) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevelArray,
            ARRAYSIZE(featureLevelArray),
            D3D11_SDK_VERSION,
            &m_pDevice,
            &featureLevel,
            &m_pContext
        );
    }
    if (FAILED(hr) || !m_pDevice || !m_pContext)
        return false;

    IDXGIDevice1* dxgiDevice1 = nullptr;
    if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice1))) && dxgiDevice1) {
        dxgiDevice1->SetMaximumFrameLatency(1);
        dxgiDevice1->Release();
    }

    if (m_canvasTransparentBackground) {
        if (!CreateCanvasCompositionSurface(m_canvasHWnd, m_canvasWidth, m_canvasHeight,
                &m_canvasSwapChain, &m_canvasRenderTargetView, &m_dcompTarget, &m_dcompVisual)) {
            Diagnostics::Error("DirectComposition transparent overlay initialization failed; falling back to opaque black mode.");
            HWND fallbackCanvas = CreateCanvasWindow(false, GetModuleHandleW(nullptr));
            if (!fallbackCanvas)
                return false;
            HWND failedCanvas = m_canvasHWnd;
            m_canvasHWnd = fallbackCanvas;
            m_canvasTransparentBackground = false;
            m_desiredCanvasTransparentBackground = false;
            OW::Config::overlayTransparentBackground = false;
            ShowWindow(m_canvasHWnd, SW_SHOW);
            SetWindowPos(m_canvasHWnd, HWND_TOPMOST, m_canvasX, m_canvasY,
                         static_cast<int>(m_canvasWidth), static_cast<int>(m_canvasHeight),
                         SWP_SHOWWINDOW);
            if (m_menuHWnd && IsWindow(m_menuHWnd)) {
                SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            m_recreatingCanvasWindow = true;
            DestroyWindow(failedCanvas);
            m_recreatingCanvasWindow = false;
        } else {
            m_canvasSwapChainFlags = 0;
            m_canvasAllowTearing = false;
        }
    }

    if (!m_canvasTransparentBackground) {
        if (!CreateSwapChainForWindow(m_canvasHWnd, m_canvasWidth, m_canvasHeight,
                true, &m_canvasSwapChain, &m_canvasSwapChainFlags,
                &m_canvasAllowTearing, false))
            return false;

        if (!CreateRenderTarget(m_canvasSwapChain, &m_canvasRenderTargetView))
            return false;
    }

    bool menuAllowTearing = false;
    if (!CreateSwapChainForWindow(m_menuHWnd, m_menuWidth, m_menuHeight, false,
            &m_menuSwapChain, &m_menuSwapChainFlags, &menuAllowTearing, false))
        return false;

    return CreateRenderTarget(m_menuSwapChain, &m_menuRenderTargetView);
}

void Overlay::CleanupDX11() {
    if (m_pContext)
        m_pContext->OMSetRenderTargets(0, nullptr, nullptr);

    ReleaseRenderTarget(&m_menuRenderTargetView);
    ReleaseRenderTarget(&m_canvasRenderTargetView);
    ReleaseCanvasCompositionResources(&m_dcompTarget, &m_dcompVisual);

    if (m_dcompDevice) {
        m_dcompDevice->Commit();
        m_dcompDevice->Release();
        m_dcompDevice = nullptr;
    }

    if (m_menuSwapChain) {
        m_menuSwapChain->Release();
        m_menuSwapChain = nullptr;
    }
    if (m_canvasSwapChain) {
        m_canvasSwapChain->Release();
        m_canvasSwapChain = nullptr;
    }
    m_canvasSwapChainFlags = 0;
    m_menuSwapChainFlags = 0;
    m_canvasAllowTearing = false;
    if (m_pContext) {
        m_pContext->Release();
        m_pContext = nullptr;
    }
    if (m_pDevice) {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
}

bool Overlay::EnsureDCompDevice() {
    if (m_dcompDevice)
        return true;
    if (!m_pDevice)
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr) || !dxgiDevice) {
        Diagnostics::Error("Overlay DirectComposition failed to query IDXGIDevice: hr=0x%08lx.", hr);
        return false;
    }

    hr = DCompositionCreateDevice(dxgiDevice, IID_PPV_ARGS(&m_dcompDevice));
    dxgiDevice->Release();
    if (FAILED(hr) || !m_dcompDevice) {
        m_dcompDevice = nullptr;
        Diagnostics::Error("Overlay DCompositionCreateDevice failed: hr=0x%08lx.", hr);
        return false;
    }
    return true;
}

void Overlay::ReleaseCanvasCompositionResources(IDCompositionTarget** dcompTarget,
                                                IDCompositionVisual** dcompVisual) {
    bool detached = false;
    if (dcompVisual && *dcompVisual) {
        (*dcompVisual)->SetContent(nullptr);
        (*dcompVisual)->Release();
        *dcompVisual = nullptr;
        detached = true;
    }
    if (dcompTarget && *dcompTarget) {
        (*dcompTarget)->SetRoot(nullptr);
        (*dcompTarget)->Release();
        *dcompTarget = nullptr;
        detached = true;
    }
    if (detached && m_dcompDevice)
        m_dcompDevice->Commit();
}

bool Overlay::CreateCanvasCompositionSurface(HWND hWnd, UINT width, UINT height,
                                             IDXGISwapChain** swapChain,
                                             ID3D11RenderTargetView** renderTargetView,
                                             IDCompositionTarget** dcompTarget,
                                             IDCompositionVisual** dcompVisual) {
    if (!hWnd || !m_pDevice || !m_pContext || !swapChain || !renderTargetView ||
        !dcompTarget || !dcompVisual || width == 0 || height == 0)
        return false;

    *swapChain = nullptr;
    *renderTargetView = nullptr;
    *dcompTarget = nullptr;
    *dcompVisual = nullptr;

    if (!EnsureDCompDevice())
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory2 = nullptr;

    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr))
        hr = dxgiDevice->GetParent(IID_PPV_ARGS(&adapter));
    if (SUCCEEDED(hr))
        hr = adapter->GetParent(IID_PPV_ARGS(&factory2));

    auto releaseDxgi = [&]() {
        if (factory2) factory2->Release();
        if (adapter) adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
    };

    if (FAILED(hr) || !factory2) {
        releaseDxgi();
        Diagnostics::Error("Overlay DirectComposition failed to query IDXGIFactory2: hr=0x%08lx.", hr);
        return false;
    }

    IDXGISwapChain1* swapChain1 = nullptr;
    const DXGI_FORMAT candidateFormats[] = {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,
    };
    HRESULT createHr = E_FAIL;
    for (DXGI_FORMAT format : candidateFormats) {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;

        createHr = factory2->CreateSwapChainForComposition(m_pDevice, &desc, nullptr, &swapChain1);
        if (SUCCEEDED(createHr) && swapChain1)
            break;
        swapChain1 = nullptr;
    }
    releaseDxgi();

    if (FAILED(createHr) || !swapChain1) {
        Diagnostics::Error("Overlay CreateSwapChainForComposition failed: hr=0x%08lx.", createHr);
        return false;
    }

    auto cleanupOnFail = [&]() {
        ReleaseRenderTarget(renderTargetView);
        ReleaseCanvasCompositionResources(dcompTarget, dcompVisual);
        if (swapChain1) {
            swapChain1->Release();
            swapChain1 = nullptr;
        }
        *swapChain = nullptr;
    };

    if (!CreateRenderTarget(swapChain1, renderTargetView)) {
        Diagnostics::Error("Overlay DirectComposition failed to create render target view.");
        cleanupOnFail();
        return false;
    }

    hr = m_dcompDevice->CreateTargetForHwnd(hWnd, TRUE, dcompTarget);
    if (FAILED(hr) || !*dcompTarget) {
        Diagnostics::Error("Overlay DirectComposition CreateTargetForHwnd failed: hr=0x%08lx.", hr);
        cleanupOnFail();
        return false;
    }

    hr = m_dcompDevice->CreateVisual(dcompVisual);
    if (FAILED(hr) || !*dcompVisual) {
        Diagnostics::Error("Overlay DirectComposition CreateVisual failed: hr=0x%08lx.", hr);
        cleanupOnFail();
        return false;
    }

    hr = (*dcompVisual)->SetContent(swapChain1);
    if (FAILED(hr)) {
        Diagnostics::Error("Overlay DirectComposition SetContent failed: hr=0x%08lx.", hr);
        cleanupOnFail();
        return false;
    }

    hr = (*dcompTarget)->SetRoot(*dcompVisual);
    if (FAILED(hr)) {
        Diagnostics::Error("Overlay DirectComposition SetRoot failed: hr=0x%08lx.", hr);
        cleanupOnFail();
        return false;
    }

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_pContext->OMSetRenderTargets(1, renderTargetView, nullptr);
    m_pContext->ClearRenderTargetView(*renderTargetView, clearColor);
    m_pContext->OMSetRenderTargets(0, nullptr, nullptr);
    swapChain1->Present(0, 0);

    hr = m_dcompDevice->Commit();
    if (FAILED(hr)) {
        Diagnostics::Error("Overlay DirectComposition Commit failed: hr=0x%08lx.", hr);
        cleanupOnFail();
        return false;
    }

    *swapChain = swapChain1;
    return true;
}

bool Overlay::RebindCanvasImGuiWindow(HWND hWnd) {
    if (!m_canvasContext || !hWnd)
        return true;

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_canvasContext);
    const ImGuiIO& io = ImGui::GetIO();
    if (io.BackendPlatformUserData)
        ImGui_ImplWin32_Shutdown();
    const bool ok = ImGui_ImplWin32_Init(hWnd);
    ImGui::SetCurrentContext(previousContext);
    return ok;
}

bool Overlay::ApplyPendingCanvasModeChange() {
    m_canvasModeChangePending = false;
    const bool targetTransparent = m_desiredCanvasTransparentBackground;
    if (m_canvasTransparentBackground == targetTransparent)
        return true;
    if (!m_canvasHWnd || !m_pDevice || !m_pContext) {
        m_canvasTransparentBackground = targetTransparent;
        return true;
    }

    HWND newCanvasHWnd = CreateCanvasWindow(targetTransparent, GetModuleHandleW(nullptr));
    if (!newCanvasHWnd) {
        Diagnostics::Error("Overlay background mode change failed to create canvas window; keeping previous mode.");
        m_desiredCanvasTransparentBackground = m_canvasTransparentBackground;
        OW::Config::overlayTransparentBackground = m_canvasTransparentBackground;
        return false;
    }

    IDXGISwapChain* newSwapChain = nullptr;
    ID3D11RenderTargetView* newRtv = nullptr;
    IDCompositionTarget* newDcompTarget = nullptr;
    IDCompositionVisual* newDcompVisual = nullptr;
    UINT newSwapChainFlags = 0;
    bool newAllowTearing = false;

    bool created = false;
    if (targetTransparent) {
        created = CreateCanvasCompositionSurface(
            newCanvasHWnd, m_canvasWidth, m_canvasHeight,
            &newSwapChain, &newRtv, &newDcompTarget, &newDcompVisual);
    } else {
        created = CreateSwapChainForWindow(
            newCanvasHWnd, m_canvasWidth, m_canvasHeight, true,
            &newSwapChain, &newSwapChainFlags, &newAllowTearing, false) &&
            CreateRenderTarget(newSwapChain, &newRtv);
        if (created) {
            const float clearColor[4] = { 1.0f / 255, 1.0f / 255, 1.0f / 255, 1.0f };
            m_pContext->OMSetRenderTargets(1, &newRtv, nullptr);
            m_pContext->ClearRenderTargetView(newRtv, clearColor);
            m_pContext->OMSetRenderTargets(0, nullptr, nullptr);
            newSwapChain->Present(0, 0);
        }
    }

    if (!created) {
        ReleaseRenderTarget(&newRtv);
        ReleaseCanvasCompositionResources(&newDcompTarget, &newDcompVisual);
        if (newSwapChain)
            newSwapChain->Release();
        m_recreatingCanvasWindow = true;
        DestroyWindow(newCanvasHWnd);
        m_recreatingCanvasWindow = false;
        m_desiredCanvasTransparentBackground = m_canvasTransparentBackground;
        OW::Config::overlayTransparentBackground = m_canvasTransparentBackground;
        Diagnostics::Error("Overlay background mode change failed; keeping the previous mode.");
        return false;
    }

    if (!RebindCanvasImGuiWindow(newCanvasHWnd)) {
        RebindCanvasImGuiWindow(m_canvasHWnd);
        ReleaseRenderTarget(&newRtv);
        ReleaseCanvasCompositionResources(&newDcompTarget, &newDcompVisual);
        if (newSwapChain)
            newSwapChain->Release();
        m_recreatingCanvasWindow = true;
        DestroyWindow(newCanvasHWnd);
        m_recreatingCanvasWindow = false;
        m_desiredCanvasTransparentBackground = m_canvasTransparentBackground;
        OW::Config::overlayTransparentBackground = m_canvasTransparentBackground;
        Diagnostics::Error("Overlay background mode change failed to rebind ImGui window; keeping the previous mode.");
        return false;
    }

    m_pContext->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseRenderTarget(&m_canvasRenderTargetView);
    ReleaseCanvasCompositionResources(&m_dcompTarget, &m_dcompVisual);
    if (m_canvasSwapChain) {
        m_canvasSwapChain->Release();
        m_canvasSwapChain = nullptr;
    }

    HWND oldCanvasHWnd = m_canvasHWnd;
    m_canvasHWnd = newCanvasHWnd;
    m_canvasSwapChain = newSwapChain;
    m_canvasRenderTargetView = newRtv;
    m_dcompTarget = newDcompTarget;
    m_dcompVisual = newDcompVisual;
    m_canvasSwapChainFlags = newSwapChainFlags;
    m_canvasAllowTearing = newAllowTearing;
    m_canvasTransparentBackground = targetTransparent;

    if (!m_minimizedToTaskbar) {
        ShowWindow(m_canvasHWnd, SW_SHOWNOACTIVATE);
        SetWindowPos(m_canvasHWnd, HWND_TOPMOST, m_canvasX, m_canvasY,
                     static_cast<int>(m_canvasWidth), static_cast<int>(m_canvasHeight),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        DWORD foregroundPid = 0;
        if (HWND foreground = GetForegroundWindow())
            GetWindowThreadProcessId(foreground, &foregroundPid);
        if (foregroundPid == GetCurrentProcessId()) {
            SetWindowsTopmost(true);
        } else if (IsWindowVisibleAndRestored(m_menuHWnd) &&
                   (GetWindowLongPtrW(m_menuHWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) {
            SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    m_recreatingCanvasWindow = true;
    DestroyWindow(oldCanvasHWnd);
    m_recreatingCanvasWindow = false;

    m_lastCanvasBoundsRefreshMs = 0;
    m_forceCanvasRender = true;
    Diagnostics::Info("Overlay background mode switched to %s.",
                      m_canvasTransparentBackground ? "transparent (DirectComposition)" : "opaque black");
    return true;
}

bool Overlay::SetCanvasTransparentBackground(bool enabled) {
    m_desiredCanvasTransparentBackground = enabled;
    if (!m_canvasHWnd || !m_pDevice) {
        m_canvasTransparentBackground = enabled;
        m_canvasModeChangePending = false;
        return true;
    }

    m_canvasModeChangePending = (m_desiredCanvasTransparentBackground != m_canvasTransparentBackground);
    if (!m_canvasModeChangePending)
        return true;

    // Never destroy or recreate HWND / DXGI SwapChain synchronously while inside
    // RenderMenu() / RenderCanvas(); apply cleanly between frames in Overlay::Run().
    if (m_inFrame)
        return true;

    return ApplyPendingCanvasModeChange();
}

bool Overlay::CreateSwapChainForWindow(HWND hWnd, UINT width, UINT height, bool allowTearingPreferred,
                                       IDXGISwapChain** swapChain, UINT* flagsOut, bool* allowTearingOut,
                                       bool forceDiscard) {
    if (flagsOut) *flagsOut = 0;
    if (allowTearingOut) *allowTearingOut = false;
    if (!m_pDevice || !hWnd || !swapChain || width == 0 || height == 0)
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;

    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr))
        hr = dxgiDevice->GetParent(IID_PPV_ARGS(&adapter));
    if (SUCCEEDED(hr))
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));

    if (FAILED(hr) || !factory) {
        if (factory)
            factory->Release();
        if (adapter)
            adapter->Release();
        if (dxgiDevice)
            dxgiDevice->Release();
        return false;
    }

    factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
    const bool tearingSupported = allowTearingPreferred && QueryAllowTearingSupport(factory);

    auto tryCreate = [&](bool flipModel, bool allowTearing) {
        DXGI_SWAP_CHAIN_DESC desc = MakeSwapChainDesc(hWnd, width, height, flipModel, allowTearing);
        const HRESULT createHr = factory->CreateSwapChain(m_pDevice, &desc, swapChain);
        if (SUCCEEDED(createHr) && *swapChain) {
            if (flagsOut) *flagsOut = desc.Flags;
            if (allowTearingOut) *allowTearingOut = flipModel && allowTearing;
            return true;
        }
        *swapChain = nullptr;
        return false;
    };

    bool created = false;
    if (!forceDiscard && tearingSupported)
        created = tryCreate(true, true);
    if (!forceDiscard && !created)
        created = tryCreate(true, false);
    if (!created)
        created = tryCreate(false, false);

    factory->Release();
    adapter->Release();
    dxgiDevice->Release();

    return created && *swapChain;
}

bool Overlay::CreateRenderTarget(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView) {
    if (!m_pDevice || !swapChain || !renderTargetView)
        return false;

    ReleaseRenderTarget(renderTargetView);

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || !backBuffer)
        return false;

    hr = m_pDevice->CreateRenderTargetView(backBuffer, nullptr, renderTargetView);
    backBuffer->Release();

    return SUCCEEDED(hr) && *renderTargetView;
}

void Overlay::ReleaseRenderTarget(ID3D11RenderTargetView** renderTargetView) {
    if (renderTargetView && *renderTargetView) {
        (*renderTargetView)->Release();
        *renderTargetView = nullptr;
    }
}

bool Overlay::ResizeSwapChain(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView,
                              UINT width, UINT height, UINT swapChainFlags) {
    if (!swapChain || !renderTargetView || width == 0 || height == 0)
        return false;

    if (m_pContext)
        m_pContext->OMSetRenderTargets(0, nullptr, nullptr);

    ReleaseRenderTarget(renderTargetView);

    HRESULT hr = swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapChainFlags);
    if (FAILED(hr))
        return false;

    return CreateRenderTarget(swapChain, renderTargetView);
}

bool Overlay::InitializeImGuiContext(ImGuiContext** context, HWND hWnd) {
    if (!context || !hWnd || !m_pDevice || !m_pContext)
        return false;

    *context = ImGui::CreateContext();
    ImGui::SetCurrentContext(*context);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    if (hWnd == m_canvasHWnd)
        ConfigureCanvasFont(io);

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hWnd))
        return false;
    if (!ImGui_ImplDX11_Init(m_pDevice, m_pContext))
        return false;

    return true;
}

void Overlay::ShutdownImGuiContext(ImGuiContext*& context) {
    if (!context)
        return;

    ImGui::SetCurrentContext(context);
    const ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData)
        ImGui_ImplDX11_Shutdown();
    if (io.BackendPlatformUserData)
        ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(context);
    context = nullptr;
    ImGui::SetCurrentContext(nullptr);
}

void Overlay::UpdateCanvasBounds(bool force) {
    // 收起时连 force 刷新也不能往下走：后面的 SWP_SHOWWINDOW 会让隐藏画布重新盖住桌面。
    if (!m_canvasHWnd || m_minimizedToTaskbar)
        return;

    const ULONGLONG now = GetTickCount64();
    if (!force && m_lastCanvasBoundsRefreshMs != 0 &&
        now - m_lastCanvasBoundsRefreshMs < kCanvasBoundsRefreshMs) {
        return;
    }
    m_lastCanvasBoundsRefreshMs = now;

    const CanvasBounds bounds = ResolveCanvasBounds();
    OW::RefreshScreenSizeFromConfig();

    RECT currentRect = {};
    if (!GetWindowRect(m_canvasHWnd, &currentRect))
        return;

    const int currentWidth = currentRect.right - currentRect.left;
    const int currentHeight = currentRect.bottom - currentRect.top;
    const bool boundsChanged =
        currentRect.left != bounds.x ||
        currentRect.top != bounds.y ||
        currentWidth != static_cast<int>(bounds.width) ||
        currentHeight != static_cast<int>(bounds.height);

    m_canvasX = bounds.x;
    m_canvasY = bounds.y;

    if (boundsChanged) {
        SetWindowPos(m_canvasHWnd, nullptr, bounds.x, bounds.y,
                     static_cast<int>(bounds.width), static_cast<int>(bounds.height),
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }

    RECT finalRect = currentRect;
    if (boundsChanged)
        GetWindowRect(m_canvasHWnd, &finalRect);

    RECT clientRect = {};
    UINT clientWidth = 0;
    UINT clientHeight = 0;
    if (GetClientRect(m_canvasHWnd, &clientRect)) {
        clientWidth = static_cast<UINT>((std::max)(0L, clientRect.right - clientRect.left));
        clientHeight = static_cast<UINT>((std::max)(0L, clientRect.bottom - clientRect.top));
    }

    const int finalWidth = finalRect.right - finalRect.left;
    const int finalHeight = finalRect.bottom - finalRect.top;
    Diagnostics::RecordOverlayCanvasBounds(
        finalRect.left,
        finalRect.top,
        static_cast<UINT>((std::max)(0, finalWidth)),
        static_cast<UINT>((std::max)(0, finalHeight)),
        clientWidth,
        clientHeight,
        IsWindowVisible(m_canvasHWnd) != FALSE,
        boundsChanged);
}

void Overlay::ApplyDesiredMenuClientSize() {
    if (m_minimizedToTaskbar || !IsWindowVisibleAndRestored(m_menuHWnd))
        return;

    const UI::MenuClientSize desired = UI::DesiredMenuClientSize();
    if(desired.width<=0 || desired.height<=0) return;
    int desiredClientW = MaxInt(1, RoundClientSize(desired.width));
    int desiredClientH = MaxInt(kMinimumMenuClientHeight, RoundClientSize(desired.height));

    RECT clientRect = {};
    RECT windowRect = {};
    if (!GetClientRect(m_menuHWnd, &clientRect) || !GetWindowRect(m_menuHWnd, &windowRect))
        return;

    const int currentClientW = clientRect.right - clientRect.left;
    const int currentClientH = clientRect.bottom - clientRect.top;
    if (currentClientW <= 0 || currentClientH <= 0)
        return;

    const int windowW = windowRect.right - windowRect.left;
    const int windowH = windowRect.bottom - windowRect.top;
    const int nonClientW = windowW - currentClientW;
    const int nonClientH = windowH - currentClientH;
    m_menuAnchorX = windowRect.left;
    m_menuAnchorY = windowRect.top;
    m_menuAnchorValid = true;

    RECT workArea = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        const int maxWindowH = workArea.bottom - windowRect.top;
        if (maxWindowH > nonClientH + kMinimumMenuClientHeight)
            desiredClientH = MinInt(desiredClientH, maxWindowH - nonClientH);
    }

    const int targetWindowW = desiredClientW + nonClientW;
    const int targetWindowH = desiredClientH + nonClientH;
    if (AbsInt(windowW - targetWindowW) <= 1 && AbsInt(windowH - targetWindowH) <= 1) {
        m_menuAnchorX = windowRect.left;
        m_menuAnchorY = windowRect.top;
        return;
    }

    SetWindowPos(m_menuHWnd, nullptr, m_menuAnchorX, m_menuAnchorY,
                 targetWindowW, targetWindowH,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

void Overlay::UpdateSwapChainSizes() {
    RECT canvasRect = {};
    if (GetClientRect(m_canvasHWnd, &canvasRect)) {
        const UINT width = static_cast<UINT>(canvasRect.right - canvasRect.left);
        const UINT height = static_cast<UINT>(canvasRect.bottom - canvasRect.top);
        if (width > 0 && height > 0 && (width != m_canvasWidth || height != m_canvasHeight)) {
            if (ResizeSwapChain(m_canvasSwapChain, &m_canvasRenderTargetView, width, height, m_canvasSwapChainFlags)) {
                m_canvasWidth = width;
                m_canvasHeight = height;
                if (m_canvasTransparentBackground && m_dcompDevice)
                    m_dcompDevice->Commit();
                Diagnostics::RecordOverlayCanvasFrame(m_canvasWidth, m_canvasHeight, 0.0f, 0.0f, true);
            }
        }
    }

    ApplyDesiredMenuClientSize();

    RECT menuRect = {};
    if (GetClientRect(m_menuHWnd, &menuRect)) {
        const UINT width = static_cast<UINT>(menuRect.right - menuRect.left);
        const UINT height = static_cast<UINT>(menuRect.bottom - menuRect.top);
        if (width > 0 && height > 0 && (width != m_menuWidth || height != m_menuHeight)) {
            if (ResizeSwapChain(m_menuSwapChain, &m_menuRenderTargetView, width, height, m_menuSwapChainFlags)) {
                m_menuWidth = width;
                m_menuHeight = height;
            }
        }
    }
}

void Overlay::RenderCanvas(std::function<void()> renderCallback) {
    if (m_minimizedToTaskbar || !m_canvasContext || !m_canvasRenderTargetView || !m_canvasSwapChain)
        return;

    struct ScopedFrameFlag {
        bool& flag;
        bool previous;
        explicit ScopedFrameFlag(bool& target) : flag(target), previous(target) { flag = true; }
        ~ScopedFrameFlag() { flag = previous; }
    } frameGuard(m_inFrame);

    OW::BeginScreenSizeFrame();
    const auto t0 = std::chrono::steady_clock::now();

    ImGui::SetCurrentContext(m_canvasContext);
    const auto fonts=V2::HeroSettings::Snapshot();
    static std::string fontPath,chinesePath;
    if(fontPath!=fonts->display.font||chinesePath!=fonts->display.chineseFont) {
        fontPath=fonts->display.font;chinesePath=fonts->display.chineseFont;
        ImGui::GetIO().Fonts->Clear();ConfigureCanvasFont(ImGui::GetIO());
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    Diagnostics::RecordOverlayCanvasFrame(m_canvasWidth, m_canvasHeight, io.DisplaySize.x, io.DisplaySize.y, false);

    {
        Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::RenderCanvas);
        if (renderCallback)
            renderCallback();
    }

    ImGui::Render();

    const auto t1 = std::chrono::steady_clock::now();

    const float clearColor[4] = {
        m_canvasTransparentBackground ? 0.0f : 1.0f / 255,
        m_canvasTransparentBackground ? 0.0f : 1.0f / 255,
        m_canvasTransparentBackground ? 0.0f : 1.0f / 255,
        m_canvasTransparentBackground ? 0.0f : 1.0f,
    };
    m_pContext->OMSetRenderTargets(1, &m_canvasRenderTargetView, nullptr);
    m_pContext->ClearRenderTargetView(m_canvasRenderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const UINT presentFlags = m_canvasAllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT presentHr = m_canvasSwapChain->Present(0, presentFlags);
    if (presentHr == DXGI_ERROR_INVALID_CALL && presentFlags != 0)
        m_canvasSwapChain->Present(0, 0);

    const auto t2 = std::chrono::steady_clock::now();

    Diagnostics::FrameTiming timing{};
    timing.renderCallbackMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    timing.presentMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    timing.totalMs = std::chrono::duration<double, std::milli>(t2 - t0).count();
    Diagnostics::RecordFrameTiming(timing);
    OW::EndScreenSizeFrame();
}

void Overlay::RenderMenu() {
    if (m_minimizedToTaskbar || !OW::Config::Menu || !m_menuContext ||
        !m_menuRenderTargetView || !m_menuSwapChain || !IsWindowVisibleAndRestored(m_menuHWnd))
        return;

    struct ScopedFrameFlag {
        bool& flag;
        bool previous;
        explicit ScopedFrameFlag(bool& target) : flag(target), previous(target) { flag = true; }
        ~ScopedFrameFlag() { flag = previous; }
    } frameGuard(m_inFrame);

    ImGui::SetCurrentContext(m_menuContext);
    ImGui::GetStyle().FontScaleDpi=GetDpiForWindow(m_menuHWnd)/96.f;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    UI::Render();

    ImGui::Render();
    // 本帧 UI 按钮可能刚收起整层。
    if (m_minimizedToTaskbar || !OW::Config::Menu)
        return;

    const float clearColor[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
    m_pContext->OMSetRenderTargets(1, &m_menuRenderTargetView, nullptr);
    m_pContext->ClearRenderTargetView(m_menuRenderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_menuSwapChain->Present(0, 0);
}
