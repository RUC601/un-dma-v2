#include "Renderer/Overlay.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

// 【功能约定：Minimize Overlay；2026-10-19 补充，供用户阅读及后续 Agent 维护】
// 用户的用途：调试时观察 Overlay，随后临时收起整层覆盖画面，查看 Codex 的工作结果或其他软件。
// 两种操作必须区分：
//   1. 正常按 Home：只开关设置菜单，画布仍显示，方便继续观察绘制。
//   2. Minimize Overlay：隐藏整个画布，把菜单最小化到任务栏，真正让出屏幕。
// 整层收起后，按一次菜单热键（默认 Home）或从任务栏恢复，必须同时找回画布和可操作菜单。
// 菜单打开时切到外部应用也收起整层；普通关菜单、UN 自己的对话框不应触发整层收起。
// 维护约束：除非用户明确修改上述交互，后续 Agent 不得把它改回“只最小化菜单、画布继续盖着”，
// 也不得禁用收起状态下的恢复热键，或为维持 render_fps 而重新显示已经隐藏的画布。
// 改动窗口、失焦、热键或渲染循环时，应一起核对 Overlay.cpp 的相关保护及 OverlayWindowSelfTest。
// 详细用途见 docs/UNLEASHED_DEVELOPMENT_GUIDE.zh-CN.md 的“Minimize Overlay 的用途和行为约定”。
// 本文件只控制窗口展示，正式程序与原生窗口回归共用；最小化不代表停止后台连接和采集线程。
void Overlay::MinimizeToTaskbar() {
    if (m_minimizedToTaskbar || !IsWindow(m_menuHWnd) || !IsWindow(m_canvasHWnd))
        return;

    // 先记录“整层已收起”，再操作窗口：ShowWindow 会同步触发激活/尺寸消息，顺序反了可能立刻恢复。
    m_minimizedToTaskbar = true;
    m_canYieldOnDeactivate = false;
    OW::Config::Menu = false;
    if (GetCapture() == m_menuHWnd)
        ReleaseCapture();
    // 这一行负责让出整块覆盖画面；仅把下面的菜单最小化，无法实现用户查看 Codex 的用途。
    ShowWindow(m_canvasHWnd, SW_HIDE);
    // Preserve the APPWINDOW taskbar/Alt+Tab entry without reactivating it when
    // the user has already switched to another application.
    ShowWindow(m_menuHWnd, SW_SHOWMINNOACTIVE);
    Diagnostics::Info("Overlay minimized: canvas hidden; restore with menu hotkey or taskbar.");
}

void Overlay::RestoreFromTaskbar() {
    if (!m_minimizedToTaskbar)
        return;

    // 画布和菜单必须一起恢复；先退出收起状态，避免同步消息重入时重复恢复。
    m_minimizedToTaskbar = false;
    m_canYieldOnDeactivate = false;
    m_lastCanvasBoundsRefreshMs = 0;
    m_lastMenuRenderMs = 0;
    OW::Config::Menu = true;

    ShowWindow(m_canvasHWnd, SW_SHOWNOACTIVATE);
    ShowWindow(m_menuHWnd, SW_RESTORE);
    SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_menuHWnd);
    Diagnostics::Info("Overlay restored: canvas and menu visible.");
}

void Overlay::ToggleMenu() {
    // 收起状态下的一次 Home 是“恢复整层”，不能只翻转 Menu，也不能随后再把刚恢复的菜单关掉。
    if (m_minimizedToTaskbar) {
        RestoreFromTaskbar();
        return;
    }
    m_canYieldOnDeactivate = false;
    OW::Config::Menu = !OW::Config::Menu.load(std::memory_order_acquire);
    UpdateWindowVisibility();
}

void Overlay::HideMenu() {
    // 普通关菜单后仍要观察画布；先取消失焦自动收起，避免自己隐藏菜单产生的失焦被误判成切换软件。
    m_canYieldOnDeactivate = false;
    OW::Config::Menu = false;
    UpdateWindowVisibility();
}

void Overlay::OnMenuActivated() {
    RestoreFromTaskbar();
}

void Overlay::OnMenuDeactivated(HWND activatedWindow) {
    // Startup and our own hide/restore can generate WM_ACTIVATE. Only yield
    // after a real menu frame, and keep same-process dialogs usable.
    if (!m_canYieldOnDeactivate)
        return;
    DWORD processId = 0;
    if (activatedWindow) {
        GetWindowThreadProcessId(activatedWindow, &processId);
        if (processId == GetCurrentProcessId())
            return;
    }
    MinimizeToTaskbar();
}

void Overlay::UpdateWindowVisibility() {
    if (!m_menuHWnd || m_minimizedToTaskbar)
        return;

    const bool shouldShowMenu = OW::Config::Menu;
    const bool isVisible = IsWindowVisible(m_menuHWnd) != FALSE;
    if (shouldShowMenu && !isVisible) {
        m_canYieldOnDeactivate = false;
        ShowWindow(m_menuHWnd, SW_SHOW);
        SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    } else if (!shouldShowMenu && isVisible) {
        m_canYieldOnDeactivate = false;
        ShowWindow(m_menuHWnd, SW_HIDE);
    }
}
