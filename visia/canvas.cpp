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

    std::array<double, 4> Canvas::bounds(const Selection item) const {
        if (item.kind == Selection::Kind::picture) {
            const auto& picture = pictures[item.index];
            return {picture.x, picture.y, picture.x + picture.width * picture.scale, picture.y + picture.height * picture.scale};
        }
        if (item.kind == Selection::Kind::text) {
            const auto& text = texts[item.index];
            return {text.x, text.y, text.x + text.width, text.y + text.height};
        }
        if (gesture == Gesture::move && !drag_group_bounds.empty()) {
            auto box = drag_group_bounds[item.index];
            if (moving_groups[item.index]) {
                box[0] += drag_x;
                box[1] += drag_y;
                box[2] += drag_x;
                box[3] += drag_y;
            }
            return box;
        }
        const auto id = groups[item.index].id;
        std::array<double, 4> box{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
        const auto include = [&](const std::array<double, 4>& child) {
            box[0] = std::min(box[0], child[0]);
            box[1] = std::min(box[1], child[1]);
            box[2] = std::max(box[2], child[2]);
            box[3] = std::max(box[3], child[3]);
        };
        for (std::size_t i = 0; i < pictures.size(); ++i)
            if (pictures[i].parent == id) include(bounds({Selection::Kind::picture, i}));
        for (std::size_t i = 0; i < texts.size(); ++i)
            if (texts[i].parent == id) include(bounds({Selection::Kind::text, i}));
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i].parent == id) include(bounds({Selection::Kind::group, i}));
        return {std::floor(box[0] / grid) * grid - grid, std::floor(box[1] / grid) * grid - grid,
            std::ceil(box[2] / grid) * grid + grid, std::ceil(box[3] / grid) * grid + grid};
    }

    std::uint64_t Canvas::group_at(const double world_x, const double world_y) const {
        std::uint64_t hit{};
        std::size_t best_depth{};
        for (std::size_t i = groups.size(); i > 0; --i) {
            const auto box = bounds({Selection::Kind::group, i - 1});
            if (world_x < box[0] || world_y < box[1] || world_x > box[2] || world_y > box[3]) continue;
            std::size_t depth{};
            auto parent = groups[i - 1].parent;
            while (parent) {
                ++depth;
                parent = groups[group_index(parent)].parent;
            }
            if (hit && depth <= best_depth) continue;
            hit = groups[i - 1].id;
            best_depth = depth;
        }
        return hit;
    }

    void Canvas::add(std::vector<std::uint8_t> png, const int width, const int height, const double world_x, const double world_y, const std::uint64_t parent) {
        const int common = std::gcd(width, height);
        const double steps = std::max(1.0, std::round(common / grid));
        pictures.push_back(Picture{next_id++, std::move(png), width, height, snap(world_x), snap(world_y), steps * grid / common, parent});
        selected = {{Selection::Kind::picture, pictures.size() - 1}};
        dirty = true;
    }

    void Canvas::add_text(const double world_x, const double world_y) {
        TextBlock text{next_id++, {}, snap(world_x), snap(world_y)};
        text.parent = group_at(world_x, world_y);
        texts.push_back(std::move(text));
        selected = {{Selection::Kind::text, texts.size() - 1}};
    }

    void Canvas::normalize() {
        if (pictures.empty() && texts.empty()) return;
        double minimum_x = std::numeric_limits<double>::max(), minimum_y = minimum_x;
        for (std::size_t i = 0; i < pictures.size(); ++i) {
            const auto box = bounds({Selection::Kind::picture, i});
            minimum_x = std::min(minimum_x, box[0]);
            minimum_y = std::min(minimum_y, box[1]);
        }
        for (std::size_t i = 0; i < texts.size(); ++i) {
            const auto box = bounds({Selection::Kind::text, i});
            minimum_x = std::min(minimum_x, box[0]);
            minimum_y = std::min(minimum_y, box[1]);
        }
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (!groups[i].parent) {
                const auto box = bounds({Selection::Kind::group, i});
                minimum_x = std::min(minimum_x, box[0]);
                minimum_y = std::min(minimum_y, box[1]);
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

    void Canvas::group_selection() {
        if (selected.empty()) return;
        const auto parent_of = [&](const Selection item) {
            if (item.kind == Selection::Kind::picture) return pictures[item.index].parent;
            if (item.kind == Selection::Kind::text) return texts[item.index].parent;
            return groups[item.index].parent;
        };
        auto parent = parent_of(selected.front());
        while (true) {
            bool common = true;
            for (const auto item : selected) {
                auto ancestor = parent_of(item);
                while (ancestor && ancestor != parent) ancestor = groups[group_index(ancestor)].parent;
                if (ancestor != parent) {
                    common = false;
                    break;
                }
            }
            if (common) break;
            parent = groups[group_index(parent)].parent;
        }
        std::array<bool, Group::palette.size()> occupied{};
        if (parent) occupied[groups[group_index(parent)].color] = true;
        for (const auto item : selected)
            if (item.kind == Selection::Kind::group) occupied[groups[item.index].color] = true;
        std::size_t color{};
        while (color < occupied.size() && occupied[color]) ++color;
        if (color == occupied.size()) {
            color = 0;
            if (parent && groups[group_index(parent)].color == color) ++color;
        }
        const auto id = next_id++;
        for (const auto item : selected) {
            if (item.kind == Selection::Kind::picture) pictures[item.index].parent = id;
            if (item.kind == Selection::Kind::text) texts[item.index].parent = id;
            if (item.kind == Selection::Kind::group) groups[item.index].parent = id;
        }
        groups.push_back(Group{id, parent, color});
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i].parent == id && groups[i].color == color) recolor_group(i, color);
        remove_empty_groups();
        selected = {{Selection::Kind::group, group_index(id)}};
        normalize();
        dirty = true;
    }

    void Canvas::ungroup(const std::size_t index) {
        const auto id = groups[index].id, parent = groups[index].parent;
        for (auto& picture : pictures)
            if (picture.parent == id) picture.parent = parent;
        for (auto& text : texts)
            if (text.parent == id) text.parent = parent;
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i].parent == id) {
                groups[i].parent = parent;
                if (parent && groups[i].color == groups[group_index(parent)].color) recolor_group(i, groups[group_index(parent)].color);
            }
        groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(index));
        selected.clear();
        normalize();
        dirty = true;
    }

    void Canvas::remove_empty_groups() {
        bool removed{};
        do {
            removed = false;
            for (std::size_t i = 0; i < groups.size(); ++i) {
                const auto id = groups[i].id;
                const bool has_picture = std::ranges::any_of(pictures, [id](const Picture& picture) { return picture.parent == id; });
                const bool has_text = std::ranges::any_of(texts, [id](const TextBlock& text) { return text.parent == id; });
                const bool has_group = std::ranges::any_of(groups, [id](const Group& group) { return group.parent == id; });
                if (has_picture || has_text || has_group) continue;
                groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(i));
                removed = true;
                dirty = true;
                break;
            }
        } while (removed);
    }

    void Canvas::delete_selection() {
        if (selected.empty()) return;
        std::unordered_set<std::uint64_t> removed;
        for (const auto item : selected) {
            if (item.kind == Selection::Kind::picture) removed.insert(pictures[item.index].id);
            if (item.kind == Selection::Kind::text) removed.insert(texts[item.index].id);
            if (item.kind == Selection::Kind::group) removed.insert(groups[item.index].id);
        }
        for (const auto& group : groups) {
            auto parent = group.parent;
            while (parent) {
                if (removed.contains(parent)) {
                    removed.insert(group.id);
                    break;
                }
                parent = groups[group_index(parent)].parent;
            }
        }
        std::erase_if(pictures, [&](const Picture& picture) { return removed.contains(picture.id) || removed.contains(picture.parent); });
        std::erase_if(texts, [&](const TextBlock& text) { return removed.contains(text.id) || removed.contains(text.parent); });
        std::erase_if(groups, [&](const Group& group) { return removed.contains(group.id); });
        selected.clear();
        gesture = Gesture::none;
        marquee.reset();
        drag_group_bounds.clear();
        moving_groups.clear();
        drop_target = 0;
        remove_empty_groups();
        normalize();
        dirty = true;
    }

    void Canvas::press(const double screen_x, const double screen_y, const int button, const bool duplicate_text) {
        pointer_x = screen_x;
        pointer_y = screen_y;
        const auto [world_x, world_y] = world(screen_x, screen_y);
        if (button == 2) {
            gesture = Gesture::pan;
            return;
        }
        if (button != 0) return;
        drag_group_bounds.clear();
        moving_groups.clear();
        drop_target = 0;
        if (selected.size() == 1 && selected.front().kind == Selection::Kind::picture) {
            const auto box = bounds(selected.front());
            const auto [right, bottom] = screen(box[2], box[3]);
            if (std::abs(screen_x - right) <= 12 && std::abs(screen_y - bottom) <= 12) {
                gesture = Gesture::resize_picture;
                return;
            }
        }
        if (!duplicate_text && selected.size() == 1 && selected.front().kind == Selection::Kind::text) {
            const auto box = bounds(selected.front());
            const auto [right, middle] = screen(box[2], (box[1] + box[3]) / 2);
            if (std::abs(screen_x - right) <= 12 && std::abs(screen_y - middle) <= 12) {
                gesture = Gesture::resize_text;
                return;
            }
        }
        std::optional<Selection> hit;
        if (const auto text = text_at(world_x, world_y)) hit = Selection{Selection::Kind::text, *text};
        if (!hit)
            for (std::size_t i = pictures.size(); i > 0; --i) {
                const auto box = bounds({Selection::Kind::picture, i - 1});
                if (world_x >= box[0] && world_y >= box[1] && world_x <= box[2] && world_y <= box[3]) {
                    hit = Selection{Selection::Kind::picture, i - 1};
                    break;
                }
            }
        if (!hit)
            if (const auto group = group_at(world_x, world_y)) hit = Selection{Selection::Kind::group, group_index(group)};
        if (hit) {
            const bool already_selected = std::ranges::find(selected, *hit) != selected.end();
            if (!already_selected) selected = {*hit};
            gesture = hit->kind == Selection::Kind::group && !already_selected ? Gesture::marquee_group :
                duplicate_text && selected.size() == 1 && hit->kind == Selection::Kind::text ? Gesture::duplicate_text : Gesture::move;
        } else {
            selected.clear();
            gesture = Gesture::marquee;
        }
        anchor_x = world_x;
        anchor_y = world_y;
        drag_x = drag_y = 0;
        if (gesture == Gesture::move || gesture == Gesture::duplicate_text) {
            std::vector<std::array<double, 4>> snapshot;
            snapshot.reserve(groups.size());
            for (std::size_t i = 0; i < groups.size(); ++i) snapshot.push_back(bounds({Selection::Kind::group, i}));
            drag_group_bounds = std::move(snapshot);
            moving_groups.assign(groups.size(), false);
            for (std::size_t i = 0; i < groups.size(); ++i) {
                auto parent = groups[i].id;
                while (parent) {
                    const auto index = group_index(parent);
                    if (std::ranges::find(selected, Selection{Selection::Kind::group, index}) != selected.end()) {
                        moving_groups[i] = true;
                        break;
                    }
                    parent = groups[index].parent;
                }
            }
        }
    }

    void Canvas::move(const double screen_x, const double screen_y) {
        if (gesture == Gesture::pan) {
            center_x -= (screen_x - pointer_x) / zoom;
            center_y -= (screen_y - pointer_y) / zoom;
            pointer_x = screen_x;
            pointer_y = screen_y;
            if (!pictures.empty() || !texts.empty()) dirty = true;
            return;
        }
        const auto [world_x, world_y] = world(screen_x, screen_y);
        if (gesture == Gesture::marquee || gesture == Gesture::marquee_group) {
            if (std::hypot(screen_x - pointer_x, screen_y - pointer_y) <= 6) {
                marquee.reset();
                if (gesture == Gesture::marquee) selected.clear();
                return;
            }
            marquee = std::array{std::min(anchor_x, world_x), std::min(anchor_y, world_y), std::max(anchor_x, world_x), std::max(anchor_y, world_y)};
            selected.clear();
            const auto contains = [&](const Selection item) {
                const auto box = bounds(item);
                return box[0] >= (*marquee)[0] && box[1] >= (*marquee)[1] && box[2] <= (*marquee)[2] && box[3] <= (*marquee)[3];
            };
            std::vector<bool> enclosed(groups.size(), false);
            for (std::size_t i = 0; i < groups.size(); ++i)
                enclosed[i] = contains({Selection::Kind::group, i});
            const auto inside_enclosed_group = [&](std::uint64_t parent) {
                while (parent) {
                    const auto index = group_index(parent);
                    if (enclosed[index]) return true;
                    parent = groups[index].parent;
                }
                return false;
            };
            for (std::size_t i = 0; i < groups.size(); ++i)
                if (enclosed[i] && !inside_enclosed_group(groups[i].parent)) selected.push_back({Selection::Kind::group, i});
            for (std::size_t i = 0; i < pictures.size(); ++i)
                if (contains({Selection::Kind::picture, i}) && !inside_enclosed_group(pictures[i].parent)) selected.push_back({Selection::Kind::picture, i});
            for (std::size_t i = 0; i < texts.size(); ++i)
                if (contains({Selection::Kind::text, i}) && !inside_enclosed_group(texts[i].parent)) selected.push_back({Selection::Kind::text, i});
            return;
        }
        if (gesture == Gesture::duplicate_text) {
            if (std::hypot(screen_x - pointer_x, screen_y - pointer_y) <= 6) return;
            if (snap(world_x - anchor_x) == 0 && snap(world_y - anchor_y) == 0) return;
            auto copy = texts[selected.front().index];
            copy.id = next_id++;
            texts.push_back(std::move(copy));
            selected = {{Selection::Kind::text, texts.size() - 1}};
            gesture = Gesture::move;
        }
        if (gesture == Gesture::move) {
            const double next_x = snap(world_x - anchor_x), next_y = snap(world_y - anchor_y);
            const double delta_x = next_x - drag_x, delta_y = next_y - drag_y;
            if (delta_x == 0 && delta_y == 0) return;
            const auto within = [&](std::uint64_t parent) {
                while (parent) {
                    const auto index = group_index(parent);
                    if (std::ranges::find(selected, Selection{Selection::Kind::group, index}) != selected.end()) return true;
                    parent = groups[index].parent;
                }
                return false;
            };
            for (std::size_t i = 0; i < pictures.size(); ++i)
                if (std::ranges::find(selected, Selection{Selection::Kind::picture, i}) != selected.end() || within(pictures[i].parent)) {
                    pictures[i].x += delta_x;
                    pictures[i].y += delta_y;
                }
            for (std::size_t i = 0; i < texts.size(); ++i)
                if (std::ranges::find(selected, Selection{Selection::Kind::text, i}) != selected.end() || within(texts[i].parent)) {
                    texts[i].x += delta_x;
                    texts[i].y += delta_y;
                }
            drag_x = next_x;
            drag_y = next_y;
            std::array<double, 4> selection_box{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
            for (const auto item : selected) {
                const auto box = bounds(item);
                selection_box[0] = std::min(selection_box[0], box[0]);
                selection_box[1] = std::min(selection_box[1], box[1]);
                selection_box[2] = std::max(selection_box[2], box[2]);
                selection_box[3] = std::max(selection_box[3], box[3]);
            }
            const double center_x = (selection_box[0] + selection_box[2]) / 2;
            const double center_y = (selection_box[1] + selection_box[3]) / 2;
            drop_target = 0;
            std::size_t best_depth{};
            for (std::size_t i = groups.size(); i > 0; --i) {
                if (moving_groups[i - 1]) continue;
                const auto& box = drag_group_bounds[i - 1];
                if (center_x < box[0] || center_y < box[1] || center_x > box[2] || center_y > box[3]) continue;
                std::size_t depth{};
                auto parent = groups[i - 1].parent;
                while (parent) {
                    ++depth;
                    parent = groups[group_index(parent)].parent;
                }
                if (drop_target && depth <= best_depth) continue;
                drop_target = groups[i - 1].id;
                best_depth = depth;
            }
            dirty = true;
        } else if (gesture == Gesture::resize_picture) {
            auto& picture = pictures[selected.front().index];
            const int common = std::gcd(picture.width, picture.height);
            const double horizontal = static_cast<double>(picture.width / common), vertical = static_cast<double>(picture.height / common);
            const double projection = (horizontal * (world_x - picture.x) + vertical * (world_y - picture.y)) / (grid * (horizontal * horizontal + vertical * vertical));
            picture.scale = std::max(1.0, std::round(projection)) * grid / common;
            dirty = true;
        } else if (gesture == Gesture::resize_text) {
            auto& text = texts[selected.front().index];
            text.auto_width = false;
            text.width = std::max(grid, snap(world_x - text.x));
            dirty = true;
        }
    }

    void Canvas::release() {
        const auto completed = gesture;
        const bool moved = completed == Gesture::move && (drag_x != 0 || drag_y != 0);
        std::vector<std::uint64_t> selected_groups;
        if (moved) {
            for (const auto item : selected) {
                if (item.kind == Selection::Kind::picture) pictures[item.index].parent = drop_target;
                if (item.kind == Selection::Kind::text) texts[item.index].parent = drop_target;
                if (item.kind == Selection::Kind::group) {
                    auto& group = groups[item.index];
                    selected_groups.push_back(group.id);
                    group.parent = drop_target;
                    if (drop_target && group.color == groups[group_index(drop_target)].color)
                        recolor_group(item.index, group.color);
                }
            }
        }
        gesture = Gesture::none;
        marquee.reset();
        drag_group_bounds.clear();
        moving_groups.clear();
        drop_target = 0;
        if (moved) {
            remove_empty_groups();
            std::size_t group{};
            for (auto& item : selected)
                if (item.kind == Selection::Kind::group) item.index = group_index(selected_groups[group++]);
        }
        if (completed == Gesture::move || completed == Gesture::resize_picture || completed == Gesture::resize_text) normalize();
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
        drag_group_bounds.clear();
        moving_groups.clear();
        drop_target = 0;
        marquee.reset();
        double left = std::numeric_limits<double>::max(), top = left;
        double right = std::numeric_limits<double>::lowest(), bottom = right;
        const auto include = [&](const std::array<double, 4>& box) {
            left = std::min(left, box[0]);
            top = std::min(top, box[1]);
            right = std::max(right, box[2]);
            bottom = std::max(bottom, box[3]);
        };
        for (std::size_t i = 0; i < pictures.size(); ++i)
            if (!pictures[i].parent) include(bounds({Selection::Kind::picture, i}));
        for (std::size_t i = 0; i < texts.size(); ++i)
            if (!texts[i].parent) include(bounds({Selection::Kind::text, i}));
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (!groups[i].parent) include(bounds({Selection::Kind::group, i}));
        center_x = (left + right) / 2;
        center_y = (top + bottom) / 2;
        zoom = std::min(viewport_width * 0.84 / (right - left), viewport_height * 0.84 / (bottom - top));
        dirty = true;
    }

    void Canvas::recolor_group(const std::size_t index, const std::size_t excluded) {
        auto& group = groups[index];
        for (std::size_t color = 0; color < Group::palette.size(); ++color)
            if (color != excluded && std::ranges::none_of(groups, [&](const Group& child) { return child.parent == group.id && child.color == color; })) {
                group.color = color;
                return;
            }
        group.color = (excluded + 1) % Group::palette.size();
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i].parent == group.id && groups[i].color == group.color) recolor_group(i, group.color);
    }

    std::size_t Canvas::group_index(const std::uint64_t id) const {
        return static_cast<std::size_t>(std::ranges::find(groups, id, &Group::id) - groups.begin());
    }
}
