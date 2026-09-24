module;
#include <GLFW/glfw3.h>

export module visia.app;
import visia.canvas;
import visia.renderer;
import visia.window;
import std;

export namespace visia {
    struct Application {
    private:
        struct UiLifetime {
            bool attached{};
            float scale{1};

            UiLifetime();
            ~UiLifetime();
            void attach(GLFWwindow* window);
            void begin(GLFWwindow* window);
        };

        Canvas canvas;
        WindowPlatform window{canvas};
        UiLifetime ui;
        Renderer renderer;
        bool redraw{true};
        bool running{true};
        enum class Action { none, close, open } pending{Action::none};
        std::filesystem::path pending_document;
        std::string error;

    public:
        Application();
        void load(const std::filesystem::path& path);

    private:
        bool save();
        void request(Action action, std::filesystem::path document = {});
        void perform(Action action);
        void update_title();
        void draw_ui();

    public:
        void loop();
    };
}
