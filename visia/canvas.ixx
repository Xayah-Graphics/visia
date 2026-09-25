export module visia.canvas;
import std;

export namespace visia {
    struct Picture {
        std::uint64_t id{};
        std::vector<std::uint8_t> png;
        int width{}, height{};
        double x{}, y{}, scale{1};
        std::uint64_t parent{};
    };

    struct TextBlock {
        enum class Weight { regular, bold };
        enum class Alignment { left, center, right };
        std::uint64_t id{};
        std::string content;
        double x{}, y{}, width{96}, height{96};
        bool auto_width{true};
        int font_size{96};
        Weight weight{Weight::regular};
        Alignment alignment{Alignment::left};
        std::array<std::uint8_t, 3> color{219, 221, 231};
        std::uint64_t parent{};
    };

    struct Group {
        static constexpr std::array palette{
            std::array{0.223F, 0.314F, 0.491F},
            std::array{0.191F, 0.397F, 0.328F},
            std::array{0.509F, 0.337F, 0.159F},
            std::array{0.392F, 0.275F, 0.509F},
            std::array{0.491F, 0.258F, 0.267F}
        };
        std::uint64_t id{}, parent{};
        std::size_t color{};
        bool background{true};
    };

    struct Selection {
        enum class Kind { picture, text, group } kind{};
        std::size_t index{};
        auto operator<=>(const Selection&) const = default;
    };

    struct Canvas {
        static constexpr double grid{32};

        std::vector<Picture> pictures;
        std::vector<TextBlock> texts;
        std::vector<Group> groups;
        std::filesystem::path path;
        double center_x{}, center_y{}, zoom{1};
        double viewport_width{1920}, viewport_height{1080};
        std::uint64_t next_id{1}, drop_target{};
        std::vector<Selection> selected;
        std::optional<std::array<double, 4>> marquee;
        bool dirty{};

        [[nodiscard]] std::array<double, 2> world(double screen_x, double screen_y) const;
        [[nodiscard]] std::array<double, 2> screen(double world_x, double world_y) const;
        [[nodiscard]] static double snap(double coordinate);
        [[nodiscard]] std::optional<std::size_t> text_at(double world_x, double world_y) const;
        [[nodiscard]] std::array<double, 4> bounds(Selection item) const;
        [[nodiscard]] std::uint64_t group_at(double world_x, double world_y) const;
        void add(std::vector<std::uint8_t> png, int width, int height, double world_x, double world_y, std::uint64_t parent);
        void add_text(double world_x, double world_y);
        void normalize();
        void group_selection();
        void ungroup(std::size_t index);
        void remove_empty_groups();
        void delete_selection();
        void press(double screen_x, double screen_y, int button, bool duplicate_text);
        void move(double screen_x, double screen_y);
        void release();
        void wheel(double screen_x, double screen_y, double steps);
        void fit();
        void recolor_group(std::size_t index, std::size_t excluded);

    private:
        enum class Gesture { none, pan, marquee, marquee_group, move, duplicate_text, resize_picture, resize_text } gesture{Gesture::none};
        double pointer_x{}, pointer_y{}, anchor_x{}, anchor_y{}, drag_x{}, drag_y{};
        std::vector<std::array<double, 4>> drag_group_bounds;
        std::vector<bool> moving_groups;
        [[nodiscard]] std::size_t group_index(std::uint64_t id) const;
    };
}
