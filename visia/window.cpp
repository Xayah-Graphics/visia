module;
#include <Windows.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

module visia.window;
import visia.canvas;
import std;

namespace visia {
    WindowPlatform::WindowPlatform(Canvas& source) : canvas{source} {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        handle.reset(glfwCreateWindow(1920, 1080, "Visia", nullptr, nullptr));
        if (!handle) throw std::runtime_error{"Cannot create Visia window"};
        native_window = glfwGetWin32Window(handle.get());
        SetPropW(native_window, L"VisiaWindow", this);
        original_window_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowPlatform::window_proc)));
        constexpr LONG_PTR style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        SetWindowLongPtrW(native_window, GWL_STYLE, style);
        constexpr BOOL dark = TRUE;
        DwmSetWindowAttribute(native_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        constexpr DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(native_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(native_window, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT bounds{};
        GetWindowRect(native_window, &bounds);
        const auto& area = monitor.rcWork;
        const int x = area.left + ((area.right - area.left) - (bounds.right - bounds.left)) / 2;
        const int y = area.top + ((area.bottom - area.top) - (bounds.bottom - bounds.top)) / 2;
        SetWindowPos(native_window, nullptr, x, y, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    WindowPlatform::~WindowPlatform() {
        SetWindowLongPtrW(native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_window_proc));
        RemovePropW(native_window, L"VisiaWindow");
    }

    void WindowPlatform::toggle_fullscreen() {
        if (!fullscreen) {
            windowed_style = GetWindowLongPtrW(native_window, GWL_STYLE);
            GetWindowPlacement(native_window, &windowed_placement);
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(native_window, MONITOR_DEFAULTTONEAREST), &monitor);
            fullscreen = true;
            SetWindowLongPtrW(native_window, GWL_STYLE, windowed_style & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_MAXIMIZE));
            constexpr DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(native_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
            const auto& area = monitor.rcMonitor;
            SetWindowPos(native_window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        } else {
            fullscreen = false;
            SetWindowLongPtrW(native_window, GWL_STYLE, windowed_style);
            SetWindowPlacement(native_window, &windowed_placement);
            constexpr DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
            DwmSetWindowAttribute(native_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
            SetWindowPos(native_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        }
    }

    LRESULT CALLBACK WindowPlatform::window_proc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
        auto* platform = static_cast<WindowPlatform*>(GetPropW(window, L"VisiaWindow"));
        if (!platform) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_NCCALCSIZE:
            if (wparam) return 0;
            break;
        case WM_NCHITTEST:
            {
                if (platform->fullscreen) return HTCLIENT;
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &point);
                RECT client{};
                GetClientRect(window, &client);
                if (!IsZoomed(window)) {
                    const UINT dpi = GetDpiForWindow(window);
                    const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    const bool left = point.x < border, right = point.x >= client.right - border;
                    const bool top = point.y < border, bottom = point.y >= client.bottom - border;
                    if (top && left) return HTTOPLEFT;
                    if (top && right) return HTTOPRIGHT;
                    if (bottom && left) return HTBOTTOMLEFT;
                    if (bottom && right) return HTBOTTOMRIGHT;
                    if (left) return HTLEFT;
                    if (right) return HTRIGHT;
                    if (top) return HTTOP;
                    if (bottom) return HTBOTTOM;
                }
                if (point.y >= 0 && point.y < MulDiv(48, GetDpiForWindow(window), 96)) {
                    const auto [world_x, world_y] = platform->canvas.world(point.x, point.y);
                    if (platform->canvas.text_at(world_x, world_y)) return HTCLIENT;
                    for (const auto& picture : platform->canvas.pictures)
                        if (world_x >= picture.x && world_y >= picture.y && world_x <= picture.x + picture.width * picture.scale && world_y <= picture.y + picture.height * picture.scale) return HTCLIENT;
                    return HTCAPTION;
                }
                return HTCLIENT;
            }
        case WM_GETMINMAXINFO:
            {
                MONITORINFO monitor{sizeof(MONITORINFO)};
                GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
                auto& size = *reinterpret_cast<MINMAXINFO*>(lparam);
                const auto& area = platform->fullscreen ? monitor.rcMonitor : monitor.rcWork;
                size.ptMaxPosition = {area.left - monitor.rcMonitor.left, area.top - monitor.rcMonitor.top};
                size.ptMaxSize = {area.right - area.left, area.bottom - area.top};
                return 0;
            }
        }
        return CallWindowProcW(platform->original_window_proc, window, message, wparam, lparam);
    }
}
