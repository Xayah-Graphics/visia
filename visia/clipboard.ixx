module;
#include <GLFW/glfw3.h>

export module visia.clipboard;
import visia.canvas;
import std;

export namespace visia {
    void copy_selection(const Canvas& canvas, GLFWwindow* window);
    void paste_selection(Canvas& canvas, GLFWwindow* window, double world_x, double world_y);
}
