module;
#include <GLFW/glfw3.h>
#include <imgui.h>

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
            std::array<ImFont*, 2> text_fonts{};

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
        std::optional<std::size_t> editing;
        std::string original_text;
        bool new_text{}, focus_text{};
        bool inspector_visible{};
        std::array<float, 4> inspector_bounds{};
        std::vector<std::filesystem::path> dropped_files;
        std::array<double, 2> drop_position{};
        enum class Shortcut { none, save, close, cancel_edit, fit } shortcut{Shortcut::none};

    public:
        Application();
        void load(const std::filesystem::path& path);

    private:
        bool save();
        void start_edit(std::size_t index, bool created);
        void finish_edit(bool commit);
        void handle_drop();
        void request(Action action, std::filesystem::path document = {});
        void perform(Action action);
        void update_title();
        void draw_ui();

    public:
        void loop();
    };
}
