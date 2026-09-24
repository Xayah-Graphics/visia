module visia.canvas;
import std;

namespace visia {
    std::array<double, 2> Canvas::world(const double screen_x, const double screen_y) const {
        return {center_x + (screen_x - viewport_width / 2) / zoom, center_y + (screen_y - viewport_height / 2) / zoom};
    }

    std::array<double, 2> Canvas::screen(const double world_x, const double world_y) const {
        return {(world_x - center_x) * zoom + viewport_width / 2, (world_y - center_y) * zoom + viewport_height / 2};
    }

    double Canvas::snap(const double coordinate) {
        return std::round(coordinate / grid) * grid;
    }

    std::optional<std::size_t> Canvas::text_at(const double world_x, const double world_y) const {
        for (std::size_t i = texts.size(); i > 0; --i) {
            const auto& text = texts[i - 1];
            if (world_x >= text.x && world_y >= text.y && world_x <= text.x + text.width && world_y <= text.y + text.height) return i - 1;
        }
        return std::nullopt;
    }

    void Canvas::add(std::vector<std::uint8_t> png, const int width, const int height, const double world_x, const double world_y) {
        const int common = std::gcd(width, height);
        const double steps = std::max(1.0, std::round(common / grid));
        pictures.push_back(Picture{next_id++, std::move(png), width, height, snap(world_x), snap(world_y), steps * grid / common});
        selected = Selection{Selection::Kind::picture, pictures.size() - 1};
        dirty = true;
    }

    void Canvas::add_text(const double world_x, const double world_y) {
        texts.push_back(TextBlock{next_id++, {}, snap(world_x), snap(world_y)});
        selected = Selection{Selection::Kind::text, texts.size() - 1};
    }

    void Canvas::normalize() {
        if (pictures.empty() && texts.empty()) return;
        double minimum_x = std::numeric_limits<double>::max(), minimum_y = minimum_x;
        for (const auto& picture : pictures) {
            minimum_x = std::min(minimum_x, picture.x);
            minimum_y = std::min(minimum_y, picture.y);
        }
        for (const auto& text : texts) {
            minimum_x = std::min(minimum_x, text.x);
            minimum_y = std::min(minimum_y, text.y);
        }
        if (minimum_x == 0 && minimum_y == 0) return;
        for (auto& picture : pictures) {
            picture.x -= minimum_x;
            picture.y -= minimum_y;
        }
        for (auto& text : texts) {
            text.x -= minimum_x;
            text.y -= minimum_y;
        }
        center_x -= minimum_x;
        center_y -= minimum_y;
    }

    void Canvas::press(const double screen_x, const double screen_y, const int button, const bool duplicate_text) {
        pointer_x = screen_x;
        pointer_y = screen_y;
        const auto [world_x, world_y] = world(screen_x, screen_y);
        if (button == 0 && selected && selected->kind == Selection::Kind::picture) {
            const auto& picture = pictures[selected->index];
            const auto [right, bottom] = screen(picture.x + picture.width * picture.scale, picture.y + picture.height * picture.scale);
            if (std::abs(screen_x - right) <= 12 && std::abs(screen_y - bottom) <= 12) {
                gesture = Gesture::resize_picture;
                return;
            }
        }
        if (button == 0 && !duplicate_text && selected && selected->kind == Selection::Kind::text) {
            const auto& text = texts[selected->index];
            const auto [right, middle] = screen(text.x + text.width, text.y + text.height / 2);
            if (std::abs(screen_x - right) <= 12 && std::abs(screen_y - middle) <= 12) {
                gesture = Gesture::resize_text;
                return;
            }
        }
        if (button == 0) {
            if (const auto hit = text_at(world_x, world_y)) {
                selected = Selection{Selection::Kind::text, *hit};
                gesture = duplicate_text ? Gesture::duplicate_text : Gesture::move;
                anchor_x = world_x - texts[*hit].x;
                anchor_y = world_y - texts[*hit].y;
                return;
            }
            for (std::size_t i = pictures.size(); i > 0; --i) {
                const auto& picture = pictures[i - 1];
                if (world_x >= picture.x && world_y >= picture.y && world_x <= picture.x + picture.width * picture.scale && world_y <= picture.y + picture.height * picture.scale) {
                    selected = Selection{Selection::Kind::picture, i - 1};
                    gesture = Gesture::move;
                    anchor_x = world_x - picture.x;
                    anchor_y = world_y - picture.y;
                    return;
                }
            }
            selected.reset();
        }
        if (button == 2) gesture = Gesture::pan;
    }

    void Canvas::move(const double screen_x, const double screen_y) {
        if (gesture == Gesture::duplicate_text) {
            if (std::hypot(screen_x - pointer_x, screen_y - pointer_y) <= 6) return;
            const auto [world_x, world_y] = world(screen_x, screen_y);
            const auto& original = texts[selected->index];
            if (snap(world_x - anchor_x) == original.x && snap(world_y - anchor_y) == original.y) return;
            auto copy = original;
            copy.id = next_id++;
            texts.push_back(std::move(copy));
            selected->index = texts.size() - 1;
            gesture = Gesture::move;
        }
        if (gesture == Gesture::pan) {
            center_x -= (screen_x - pointer_x) / zoom;
            center_y -= (screen_y - pointer_y) / zoom;
            pointer_x = screen_x;
            pointer_y = screen_y;
            if (!pictures.empty() || !texts.empty()) dirty = true;
        } else if (gesture == Gesture::move) {
            const auto [world_x, world_y] = world(screen_x, screen_y);
            if (selected->kind == Selection::Kind::picture) {
                auto& picture = pictures[selected->index];
                picture.x = snap(world_x - anchor_x);
                picture.y = snap(world_y - anchor_y);
            } else {
                auto& text = texts[selected->index];
                text.x = snap(world_x - anchor_x);
                text.y = snap(world_y - anchor_y);
            }
            normalize();
            dirty = true;
        } else if (gesture == Gesture::resize_picture) {
            const auto [world_x, world_y] = world(screen_x, screen_y);
            auto& picture = pictures[selected->index];
            const int common = std::gcd(picture.width, picture.height);
            const double horizontal = static_cast<double>(picture.width / common), vertical = static_cast<double>(picture.height / common);
            const double projection = (horizontal * (world_x - picture.x) + vertical * (world_y - picture.y)) / (grid * (horizontal * horizontal + vertical * vertical));
            picture.scale = std::max(1.0, std::round(projection)) * grid / common;
            dirty = true;
        } else if (gesture == Gesture::resize_text) {
            const auto [world_x, world_y] = world(screen_x, screen_y);
            auto& text = texts[selected->index];
            text.auto_width = false;
            text.width = std::max(grid, snap(world_x - text.x));
            dirty = true;
        }
    }

    void Canvas::release() {
        gesture = Gesture::none;
    }

    void Canvas::wheel(const double screen_x, const double screen_y, const double steps) {
        const auto [world_x, world_y] = world(screen_x, screen_y);
        zoom *= std::pow(1.15, steps);
        center_x = world_x - (screen_x - viewport_width / 2) / zoom;
        center_y = world_y - (screen_y - viewport_height / 2) / zoom;
        if (!pictures.empty() || !texts.empty()) dirty = true;
    }

    void Canvas::fit() {
        if (pictures.empty() && texts.empty()) return;
        gesture = Gesture::none;
        double left = std::numeric_limits<double>::max(), top = left;
        double right = std::numeric_limits<double>::lowest(), bottom = right;
        for (const auto& picture : pictures) {
            left = std::min(left, picture.x);
            top = std::min(top, picture.y);
            right = std::max(right, picture.x + picture.width * picture.scale);
            bottom = std::max(bottom, picture.y + picture.height * picture.scale);
        }
        for (const auto& text : texts) {
            left = std::min(left, text.x);
            top = std::min(top, text.y);
            right = std::max(right, text.x + text.width);
            bottom = std::max(bottom, text.y + text.height);
        }
        center_x = (left + right) / 2;
        center_y = (top + bottom) / 2;
        zoom = std::min(viewport_width * 0.84 / (right - left), viewport_height * 0.84 / (bottom - top));
        dirty = true;
    }
}
