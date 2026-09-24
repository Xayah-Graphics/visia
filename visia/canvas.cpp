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

    void Canvas::add(std::vector<std::uint8_t> png, const int width, const int height, const double world_x, const double world_y) {
        const int common = std::gcd(width, height);
        const double steps = std::max(1.0, std::round(common / grid));
        pictures.push_back(Picture{next_id++, std::move(png), width, height, snap(world_x), snap(world_y), steps * grid / common});
        selected = pictures.size() - 1;
        dirty = true;
    }

    void Canvas::normalize() {
        if (pictures.empty()) return;
        double minimum_x = pictures.front().x, minimum_y = pictures.front().y;
        for (const auto& picture : pictures) {
            minimum_x = std::min(minimum_x, picture.x);
            minimum_y = std::min(minimum_y, picture.y);
        }
        if (minimum_x == 0 && minimum_y == 0) return;
        for (auto& picture : pictures) {
            picture.x -= minimum_x;
            picture.y -= minimum_y;
        }
        center_x -= minimum_x;
        center_y -= minimum_y;
    }

    void Canvas::press(const double screen_x, const double screen_y, const int button) {
        pointer_x = screen_x;
        pointer_y = screen_y;
        const auto [world_x, world_y] = world(screen_x, screen_y);
        if (button == 0 && selected) {
            const auto& picture = pictures[*selected];
            const auto [right, bottom] = screen(picture.x + picture.width * picture.scale, picture.y + picture.height * picture.scale);
            if (std::abs(screen_x - right) <= 12 && std::abs(screen_y - bottom) <= 12) {
                gesture = Gesture::resize;
                return;
            }
        }
        if (button == 0) {
            for (std::size_t i = pictures.size(); i > 0; --i) {
                const auto& picture = pictures[i - 1];
                if (world_x >= picture.x && world_y >= picture.y && world_x <= picture.x + picture.width * picture.scale && world_y <= picture.y + picture.height * picture.scale) {
                    selected = i - 1;
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
        if (gesture == Gesture::pan) {
            center_x -= (screen_x - pointer_x) / zoom;
            center_y -= (screen_y - pointer_y) / zoom;
            pointer_x = screen_x;
            pointer_y = screen_y;
            if (!pictures.empty()) dirty = true;
        } else if (gesture == Gesture::move) {
            const auto [world_x, world_y] = world(screen_x, screen_y);
            auto& picture = pictures[*selected];
            picture.x = snap(world_x - anchor_x);
            picture.y = snap(world_y - anchor_y);
            normalize();
            dirty = true;
        } else if (gesture == Gesture::resize) {
            const auto [world_x, world_y] = world(screen_x, screen_y);
            auto& picture = pictures[*selected];
            const int common = std::gcd(picture.width, picture.height);
            const double horizontal = static_cast<double>(picture.width / common), vertical = static_cast<double>(picture.height / common);
            const double projection = (horizontal * (world_x - picture.x) + vertical * (world_y - picture.y)) / (grid * (horizontal * horizontal + vertical * vertical));
            picture.scale = std::max(1.0, std::round(projection)) * grid / common;
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
        if (!pictures.empty()) dirty = true;
    }

    void Canvas::fit() {
        if (pictures.empty()) return;
        gesture = Gesture::none;
        double left = pictures.front().x, top = pictures.front().y;
        double right = left + pictures.front().width * pictures.front().scale;
        double bottom = top + pictures.front().height * pictures.front().scale;
        for (const auto& picture : pictures) {
            left = std::min(left, picture.x);
            top = std::min(top, picture.y);
            right = std::max(right, picture.x + picture.width * picture.scale);
            bottom = std::max(bottom, picture.y + picture.height * picture.scale);
        }
        center_x = (left + right) / 2;
        center_y = (top + bottom) / 2;
        zoom = std::min(viewport_width * 0.84 / (right - left), viewport_height * 0.84 / (bottom - top));
        dirty = true;
    }
}
