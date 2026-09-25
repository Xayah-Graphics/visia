module;
#include <Windows.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <nlohmann/json.hpp>

module visia.clipboard;
import visia.canvas;
import std;

namespace visia {
    namespace {
        struct ClipboardAccess {
            explicit ClipboardAccess(GLFWwindow* window) {
                if (!OpenClipboard(glfwGetWin32Window(window))) throw std::runtime_error{"Cannot open Windows clipboard"};
            }

            ~ClipboardAccess() {
                CloseClipboard();
            }
        };

        UINT clipboard_format() {
            static const UINT format = RegisterClipboardFormatW(L"Visia.CanvasSelection");
            if (!format) throw std::runtime_error{"Cannot register Visia clipboard format"};
            return format;
        }
    }

    void copy_selection(const Canvas& canvas, GLFWwindow* window) {
        if (canvas.selected.empty()) return;
        double left = std::numeric_limits<double>::max(), top = left;
        for (const auto item : canvas.selected) {
            const auto box = canvas.bounds(item);
            left = std::min(left, box[0]);
            top = std::min(top, box[1]);
        }

        std::unordered_set<std::uint64_t> included_groups;
        for (const auto item : canvas.selected)
            if (item.kind == Selection::Kind::group) included_groups.insert(canvas.groups[item.index].id);
        bool expanded{};
        do {
            expanded = false;
            for (const auto& group : canvas.groups)
                if (included_groups.contains(group.parent) && included_groups.insert(group.id).second) expanded = true;
        } while (expanded);

        nlohmann::json fragment{{"pictures", nlohmann::json::array()}, {"texts", nlohmann::json::array()}, {"groups", nlohmann::json::array()}};
        for (std::size_t i = 0; i < canvas.pictures.size(); ++i) {
            const auto& picture = canvas.pictures[i];
            if (!included_groups.contains(picture.parent) && std::ranges::find(canvas.selected, Selection{Selection::Kind::picture, i}) == canvas.selected.end()) continue;
            fragment["pictures"].push_back({{"id", picture.id}, {"parent", included_groups.contains(picture.parent) ? picture.parent : 0},
                {"x", picture.x - left}, {"y", picture.y - top}, {"width", picture.width}, {"height", picture.height},
                {"scale", picture.scale}, {"png", nlohmann::json::binary(picture.png)}});
        }
        for (std::size_t i = 0; i < canvas.texts.size(); ++i) {
            const auto& text = canvas.texts[i];
            if (!included_groups.contains(text.parent) && std::ranges::find(canvas.selected, Selection{Selection::Kind::text, i}) == canvas.selected.end()) continue;
            fragment["texts"].push_back({{"id", text.id}, {"parent", included_groups.contains(text.parent) ? text.parent : 0},
                {"x", text.x - left}, {"y", text.y - top}, {"width", text.width}, {"height", text.height},
                {"content", text.content}, {"auto_width", text.auto_width}, {"font_size", text.font_size},
                {"font_weight", static_cast<int>(text.weight)}, {"alignment", static_cast<int>(text.alignment)}, {"color", text.color}});
        }
        for (const auto& group : canvas.groups)
            if (included_groups.contains(group.id)) fragment["groups"].push_back({{"id", group.id},
                {"parent", included_groups.contains(group.parent) ? group.parent : 0}, {"color", group.color}, {"background", group.background}});

        const auto bytes = nlohmann::json::to_cbor(fragment);
        const auto format = clipboard_format();
        ClipboardAccess access{window};
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (!memory) throw std::runtime_error{"Cannot allocate Visia clipboard data"};
        void* destination = GlobalLock(memory);
        if (!destination) {
            GlobalFree(memory);
            throw std::runtime_error{"Cannot access Visia clipboard data"};
        }
        std::memcpy(destination, bytes.data(), bytes.size());
        GlobalUnlock(memory);
        if (!EmptyClipboard() || !SetClipboardData(format, memory)) {
            GlobalFree(memory);
            throw std::runtime_error{"Cannot copy Visia selection to clipboard"};
        }
    }

    void paste_selection(Canvas& canvas, GLFWwindow* window, const double world_x, const double world_y) {
        std::vector<std::uint8_t> bytes;
        {
            ClipboardAccess access{window};
            const auto format = clipboard_format();
            if (!IsClipboardFormatAvailable(format)) throw std::runtime_error{"Clipboard does not contain a Visia selection"};
            const auto memory = GetClipboardData(format);
            if (!memory) throw std::runtime_error{"Cannot read Visia clipboard data"};
            const auto size = GlobalSize(memory);
            bytes.resize(size);
            const void* source = GlobalLock(memory);
            if (!source) throw std::runtime_error{"Cannot access Visia clipboard data"};
            std::memcpy(bytes.data(), source, size);
            GlobalUnlock(memory);
        }

        const auto fragment = nlohmann::json::from_cbor(bytes);
        const auto parent = canvas.group_at(world_x, world_y);
        const double x = Canvas::snap(world_x), y = Canvas::snap(world_y);
        std::uint64_t next_id = canvas.next_id;
        std::unordered_map<std::uint64_t, std::uint64_t> ids;
        for (const auto& item : fragment.at("groups")) ids.emplace(item.at("id").get<std::uint64_t>(), next_id++);
        for (const auto& item : fragment.at("pictures")) ids.emplace(item.at("id").get<std::uint64_t>(), next_id++);
        for (const auto& item : fragment.at("texts")) ids.emplace(item.at("id").get<std::uint64_t>(), next_id++);
        const auto mapped_parent = [&](const std::uint64_t id) { return id ? ids.at(id) : parent; };

        std::vector<Group> groups;
        std::vector<Picture> pictures;
        std::vector<TextBlock> texts;
        std::vector<Selection> selected;
        for (const auto& item : fragment.at("groups")) {
            const auto original_parent = item.at("parent").get<std::uint64_t>();
            if (!original_parent) selected.push_back({Selection::Kind::group, canvas.groups.size() + groups.size()});
            groups.push_back(Group{ids.at(item.at("id").get<std::uint64_t>()), mapped_parent(original_parent),
                item.at("color").get<std::size_t>(), item.at("background").get<bool>()});
        }
        for (const auto& item : fragment.at("pictures")) {
            const auto original_parent = item.at("parent").get<std::uint64_t>();
            if (!original_parent) selected.push_back({Selection::Kind::picture, canvas.pictures.size() + pictures.size()});
            Picture picture;
            picture.id = ids.at(item.at("id").get<std::uint64_t>());
            picture.parent = mapped_parent(original_parent);
            picture.x = x + item.at("x").get<double>();
            picture.y = y + item.at("y").get<double>();
            picture.width = item.at("width").get<int>();
            picture.height = item.at("height").get<int>();
            picture.scale = item.at("scale").get<double>();
            const auto& png = item.at("png").get_binary();
            picture.png.assign(png.begin(), png.end());
            pictures.push_back(std::move(picture));
        }
        for (const auto& item : fragment.at("texts")) {
            const auto original_parent = item.at("parent").get<std::uint64_t>();
            if (!original_parent) selected.push_back({Selection::Kind::text, canvas.texts.size() + texts.size()});
            TextBlock text;
            text.id = ids.at(item.at("id").get<std::uint64_t>());
            text.parent = mapped_parent(original_parent);
            text.x = x + item.at("x").get<double>();
            text.y = y + item.at("y").get<double>();
            text.width = item.at("width").get<double>();
            text.height = item.at("height").get<double>();
            text.content = item.at("content").get<std::string>();
            text.auto_width = item.at("auto_width").get<bool>();
            text.font_size = item.at("font_size").get<int>();
            text.weight = static_cast<TextBlock::Weight>(item.at("font_weight").get<int>());
            text.alignment = static_cast<TextBlock::Alignment>(item.at("alignment").get<int>());
            text.color = item.at("color").get<std::array<std::uint8_t, 3>>();
            texts.push_back(std::move(text));
        }

        canvas.groups.reserve(canvas.groups.size() + groups.size());
        canvas.pictures.reserve(canvas.pictures.size() + pictures.size());
        canvas.texts.reserve(canvas.texts.size() + texts.size());
        canvas.groups.insert(canvas.groups.end(), std::make_move_iterator(groups.begin()), std::make_move_iterator(groups.end()));
        canvas.pictures.insert(canvas.pictures.end(), std::make_move_iterator(pictures.begin()), std::make_move_iterator(pictures.end()));
        canvas.texts.insert(canvas.texts.end(), std::make_move_iterator(texts.begin()), std::make_move_iterator(texts.end()));
        if (parent) {
            const auto parent_color = canvas.groups[static_cast<std::size_t>(std::ranges::find(canvas.groups, parent, &Group::id) - canvas.groups.begin())].color;
            for (const auto item : selected)
                if (item.kind == Selection::Kind::group && canvas.groups[item.index].color == parent_color)
                    canvas.recolor_group(item.index, parent_color);
        }
        canvas.next_id = next_id;
        canvas.selected = std::move(selected);
        canvas.normalize();
        canvas.dirty = true;
    }
}
