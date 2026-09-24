module;
#include <Windows.h>
#include <GLFW/glfw3.h>

export module visia.window;
import visia.canvas;
import std;

export namespace visia {
    struct WindowPlatform {
        explicit WindowPlatform(Canvas& canvas);
        ~WindowPlatform();

        WindowPlatform(const WindowPlatform&) = delete;
        WindowPlatform& operator=(const WindowPlatform&) = delete;

        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> handle{nullptr, glfwDestroyWindow};
        HWND native_window{};
        void toggle_fullscreen();

    private:
        Canvas& canvas;
        WNDPROC original_window_proc{};
        bool fullscreen{};
        LONG_PTR windowed_style{};
        WINDOWPLACEMENT windowed_placement{sizeof(WINDOWPLACEMENT)};

        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    };
}
