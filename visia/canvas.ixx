export module visia.canvas;
import std;

export namespace visia {
    struct Picture {
        std::uint64_t id{};
        std::vector<std::uint8_t> png;
        int width{}, height{};
        double x{}, y{}, scale{1};
    };

    struct Canvas {
        static constexpr double grid{32};

        std::vector<Picture> pictures;
        std::filesystem::path path;
        double center_x{}, center_y{}, zoom{1};
        double viewport_width{1920}, viewport_height{1080};
        std::uint64_t next_id{1};
        std::optional<std::size_t> selected;
        bool dirty{};

        [[nodiscard]] std::array<double, 2> world(double screen_x, double screen_y) const;
        [[nodiscard]] std::array<double, 2> screen(double world_x, double world_y) const;
        [[nodiscard]] static double snap(double coordinate);
        void add(std::vector<std::uint8_t> png, int width, int height, double world_x, double world_y);
        void normalize();
        void press(double screen_x, double screen_y, int button);
        void move(double screen_x, double screen_y);
        void release();
        void wheel(double screen_x, double screen_y, double steps);
        void fit();

    private:
        enum class Gesture { none, pan, move, resize } gesture{Gesture::none};
        double pointer_x{}, pointer_y{}, anchor_x{}, anchor_y{};
    };
}
