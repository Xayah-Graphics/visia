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

    private:
        Canvas& canvas;
        WNDPROC original_window_proc{};

        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    };
}
