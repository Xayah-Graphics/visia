#include <Windows.h>
#include <GLFW/glfw3.h>
#include <nfd.h>

import std;
import visia.app;

namespace {
    struct GlfwLifetime {
        GlfwLifetime() {
            if (glfwInit() != GLFW_TRUE) throw std::runtime_error{"Cannot initialize GLFW"};
        }
        ~GlfwLifetime() { glfwTerminate(); }
    };

    struct DialogLifetime {
        DialogLifetime() {
            if (NFD_Init() != NFD_OKAY) throw std::runtime_error{NFD_GetError()};
        }
        ~DialogLifetime() { NFD_Quit(); }
    };
}

int main(const int argc, char** argv) {
    try {
        GlfwLifetime glfw;
        DialogLifetime dialogs;
        visia::Application app;
        if (argc > 1) app.load(std::filesystem::path{argv[1]});
        app.loop();
        return 0;
    } catch (const std::exception& failure) {
        MessageBoxA(nullptr, failure.what(), "Visia failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
