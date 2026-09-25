module;
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

module visia.app;
import visia.canvas;
import visia.clipboard;
import visia.document;
import visia.renderer;
import visia.window;
import std;

namespace visia {
    namespace {
        std::filesystem::path executable_directory() {
            std::wstring executable(32768, L'\0');
            executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
            return std::filesystem::path{executable}.parent_path();
        }
    }

    Application::UiLifetime::UiLifetime() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/SegUIVar.ttf");
        constexpr std::array names{L"NotoSansSC-Regular.otf", L"NotoSansSC-Bold.otf"};
        for (std::size_t weight = 0; weight < names.size(); ++weight) {
            const auto path = (executable_directory() / names[weight]).u8string();
            text_fonts[weight] = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()));
            if (!text_fonts[weight]) throw std::runtime_error{"Cannot load Visia text font"};
        }
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 14;
        style.PopupRounding = 14;
        style.FrameRounding = 8;
        style.WindowBorderSize = 1;
        style.WindowPadding = {22, 19};
        style.FramePadding = {13, 9};
        style.ItemSpacing = {10, 14};
        style.Colors[ImGuiCol_Text] = {0.82F, 0.84F, 0.90F, 1};
        style.Colors[ImGuiCol_TextDisabled] = {0.29F, 0.32F, 0.39F, 1};
        style.Colors[ImGuiCol_PopupBg] = {0.018F, 0.021F, 0.029F, 0.98F};
        style.Colors[ImGuiCol_WindowBg] = {0.018F, 0.021F, 0.029F, 0.98F};
        style.Colors[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.58F};
        style.Colors[ImGuiCol_Border] = {0.28F, 0.30F, 0.39F, 0.33F};
        style.Colors[ImGuiCol_Button] = {0.10F, 0.11F, 0.16F, 1};
        style.Colors[ImGuiCol_ButtonHovered] = {0.20F, 0.21F, 0.29F, 1};
        style.Colors[ImGuiCol_ButtonActive] = {0.28F, 0.28F, 0.40F, 1};
        style.Colors[ImGuiCol_NavCursor] = {0.39F, 0.39F, 0.62F, 1};
    }

    Application::UiLifetime::~UiLifetime() {
        if (attached) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void Application::UiLifetime::attach(GLFWwindow* window) {
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error{"Cannot initialize Visia UI input"};
        attached = true;
    }

    void Application::UiLifetime::begin(GLFWwindow* window) {
        float current{}, vertical{};
        glfwGetWindowContentScale(window, &current, &vertical);
        if (current != scale) {
            auto& style = ImGui::GetStyle();
            style.ScaleAllSizes(current / scale);
            style.FontScaleDpi = current;
            scale = current;
        }
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    Application::Application() : renderer{window.handle.get()} {
        auto* source = window.handle.get();
        glfwSetWindowUserPointer(source, this);
        int width{}, height{};
        glfwGetWindowSize(source, &width, &height);
        canvas.viewport_width = width;
        canvas.viewport_height = height;
        glfwSetWindowSizeCallback(source, [](GLFWwindow* source, const int width, const int height) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.canvas.viewport_width = width;
            app.canvas.viewport_height = height;
            app.redraw = true;
        });
        glfwSetFramebufferSizeCallback(source, [](GLFWwindow* source, int, int) {
            static_cast<Application*>(glfwGetWindowUserPointer(source))->redraw = true;
        });
        glfwSetCursorPosCallback(source, [](GLFWwindow* source, const double x, const double y) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            if (app.pending == Action::none && app.error.empty() && !app.editing && !ImGui::GetIO().WantCaptureMouse) app.canvas.move(x, y);
            app.redraw = true;
        });
        glfwSetMouseButtonCallback(source, [](GLFWwindow* source, const int button, const int action, const int modifiers) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.redraw = true;
            double x{}, y{};
            glfwGetCursorPos(source, &x, &y);
            const bool inside_inspector = app.inspector_visible &&
                x >= app.inspector_bounds[0] && y >= app.inspector_bounds[1] &&
                x <= app.inspector_bounds[2] && y <= app.inspector_bounds[3];
            const bool inside_group_toolbar = app.group_toolbar_visible &&
                x >= app.group_toolbar_bounds[0] && y >= app.group_toolbar_bounds[1] &&
                x <= app.group_toolbar_bounds[2] && y <= app.group_toolbar_bounds[3];
            if (action == GLFW_RELEASE) {
                app.canvas.release();
                if (button == GLFW_MOUSE_BUTTON_LEFT && !inside_inspector && !inside_group_toolbar && !app.editing && !ImGui::GetIO().WantCaptureMouse) {
                    app.inspector_visible = app.canvas.selected.size() == 1 && app.canvas.selected.front().kind == Selection::Kind::text;
                    app.group_toolbar_visible = !app.inspector_visible && !app.canvas.selected.empty();
                }
                return;
            }
            if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && app.editing) {
                const auto& text = app.canvas.texts[*app.editing];
                const auto [left, top] = app.canvas.screen(text.x, text.y);
                const double width = (text.width + (text.auto_width ? text.font_size : 0)) * app.canvas.zoom;
                const double height = text.height * app.canvas.zoom;
                if (x < left || y < top || x > left + width || y > top + height) {
                    app.finish_edit(true);
                    app.canvas.selected.clear();
                    app.inspector_visible = false;
                    app.group_toolbar_visible = false;
                }
                return;
            }
            if (button == GLFW_MOUSE_BUTTON_LEFT && (inside_inspector || inside_group_toolbar)) return;
            if (app.pending != Action::none || !app.error.empty() || app.editing || ImGui::GetIO().WantCaptureMouse) return;
            if (action != GLFW_PRESS) return;
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                app.inspector_visible = false;
                app.group_toolbar_visible = false;
            }
            if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) app.canvas.press(x, y, button, (modifiers & GLFW_MOD_ALT) != 0);
        });
        glfwSetScrollCallback(source, [](GLFWwindow* source, double, const double steps) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.redraw = true;
            if (app.pending != Action::none || !app.error.empty() || app.editing || ImGui::GetIO().WantCaptureMouse) return;
            double x{}, y{};
            glfwGetCursorPos(source, &x, &y);
            app.canvas.wheel(x, y, steps);
        });
        glfwSetDropCallback(source, [](GLFWwindow* source, const int count, const char** paths) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            if (app.pending != Action::none || !app.error.empty()) return;
            app.dropped_files.clear();
            for (int index = 0; index < count; ++index) app.dropped_files.emplace_back(std::u8string{reinterpret_cast<const char8_t*>(paths[index])});
            glfwGetCursorPos(source, &app.drop_position[0], &app.drop_position[1]);
            app.redraw = true;
        });
        glfwSetKeyCallback(source, [](GLFWwindow* source, const int key, int, const int action, const int modifiers) {
            if (action != GLFW_PRESS) return;
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.redraw = true;
            if (app.pending != Action::none || !app.error.empty() || (modifiers & (GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER))) return;
            if (key == GLFW_KEY_ESCAPE && !(modifiers & GLFW_MOD_CONTROL)) {
                if (app.editing) app.shortcut = Shortcut::cancel_edit;
            }
            else if (key == GLFW_KEY_F11 && !(modifiers & GLFW_MOD_CONTROL)) app.window.toggle_fullscreen();
            else if (key == GLFW_KEY_W && (modifiers & GLFW_MOD_CONTROL)) app.shortcut = Shortcut::close;
            else if (key == GLFW_KEY_S && (modifiers & GLFW_MOD_CONTROL)) app.shortcut = Shortcut::save;
            else if (key == GLFW_KEY_C && (modifiers & GLFW_MOD_CONTROL) && !app.editing && !ImGui::GetIO().WantTextInput) app.shortcut = Shortcut::copy;
            else if (key == GLFW_KEY_V && (modifiers & GLFW_MOD_CONTROL) && !app.editing && !ImGui::GetIO().WantTextInput) app.shortcut = Shortcut::paste;
            else if (key == GLFW_KEY_F && !(modifiers & GLFW_MOD_CONTROL) && !app.editing) app.shortcut = Shortcut::fit;
            else if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_KP_DECIMAL) && !(modifiers & GLFW_MOD_CONTROL) && !app.editing && !ImGui::GetIO().WantTextInput) app.shortcut = Shortcut::delete_selection;
        });
        ui.attach(source);
        glfwShowWindow(source);
    }

    void Application::load(const std::filesystem::path& path) {
        Canvas opened = open_document(path);
        renderer.clear();
        canvas = std::move(opened);
        inspector_visible = false;
        group_toolbar_visible = false;
        int width{}, height{};
        glfwGetWindowSize(window.handle.get(), &width, &height);
        canvas.viewport_width = width;
        canvas.viewport_height = height;
        redraw = true;
        update_title();
    }

    bool Application::save() {
        auto destination = canvas.path;
        if (destination.empty()) {
            const auto directory = executable_directory().u8string();
            nfdu8char_t* selected{};
            const nfdu8filteritem_t filter{"Visia document", "visia"};
            const auto result = NFD_SaveDialogU8(&selected, &filter, 1, reinterpret_cast<const nfdu8char_t*>(directory.c_str()), "Untitled.visia");
            if (result == NFD_CANCEL) return false;
            if (result != NFD_OKAY) throw std::runtime_error{NFD_GetError()};
            std::unique_ptr<nfdu8char_t, decltype(&NFD_FreePathU8)> path{selected, NFD_FreePathU8};
            destination = std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(path.get())}};
        }
        save_document(canvas, destination);
        update_title();
        return true;
    }

    void Application::start_edit(const std::size_t index, const bool created) {
        editing = index;
        original_text = canvas.texts[index].content;
        new_text = created;
        focus_text = true;
        canvas.selected = {{Selection::Kind::text, index}};
        inspector_visible = false;
        group_toolbar_visible = false;
        canvas.release();
        redraw = true;
    }

    void Application::finish_edit(const bool commit) {
        if (!editing) return;
        const std::size_t index = *editing;
        auto& text = canvas.texts[index];
        const bool remove = (new_text && !commit) || (commit && text.content.empty());
        if (!commit && !new_text) text.content = original_text;
        if (remove) {
            canvas.texts.erase(canvas.texts.begin() + static_cast<std::ptrdiff_t>(index));
            canvas.selected.clear();
            canvas.remove_empty_groups();
            canvas.normalize();
            if (commit && !new_text) canvas.dirty = true;
        } else if (commit && (new_text || text.content != original_text)) canvas.dirty = true;
        inspector_visible = !remove;
        editing.reset();
        original_text.clear();
        new_text = false;
        focus_text = false;
        redraw = true;
    }

    void Application::handle_drop() {
        auto files = std::exchange(dropped_files, {});
        inspector_visible = false;
        group_toolbar_visible = false;
        try {
            const auto document = std::ranges::find_if(files, [](const std::filesystem::path& file) {
                auto extension = file.extension().wstring();
                for (auto& letter : extension) if (letter >= L'A' && letter <= L'Z') letter += L'a' - L'A';
                return extension == L".visia";
            });
            if (document != files.end()) {
                if (files.size() != 1) throw std::runtime_error{"Drop one .visia document at a time, without PNG images"};
                request(Action::open, *document);
                return;
            }
            std::vector<Picture> pictures;
            for (const auto& file : files) pictures.push_back(read_png(file));
            auto position = canvas.world(drop_position[0], drop_position[1]);
            const auto parent = canvas.group_at(position[0], position[1]);
            for (auto& picture : pictures) {
                canvas.add(std::move(picture.png), picture.width, picture.height, position[0], position[1], parent);
                const auto& placed = canvas.pictures.back();
                position[0] = Canvas::snap(placed.x + placed.width * placed.scale) + Canvas::grid;
            }
            canvas.normalize();
            redraw = true;
        } catch (const std::exception& failure) {
            error = failure.what();
            redraw = true;
        }
    }

    void Application::request(const Action action, std::filesystem::path document) {
        pending_document = std::move(document);
        if (canvas.dirty) pending = action;
        else perform(action);
        redraw = true;
    }

    void Application::perform(const Action action) {
        if (action == Action::close) {
            running = false;
            return;
        }
        if (action == Action::open) {
            load(std::exchange(pending_document, {}));
        }
    }

    void Application::update_title() {
        std::string title{"Visia — "};
        if (canvas.path.empty()) title += "Untitled";
        else {
            const auto name = canvas.path.filename().u8string();
            title.append(reinterpret_cast<const char*>(name.data()), name.size());
        }
        glfwSetWindowTitle(window.handle.get(), title.c_str());
    }

    void Application::draw_ui() {
        const auto command = std::exchange(shortcut, Shortcut::none);
        ui.begin(window.handle.get());
        const auto& io = ImGui::GetIO();
        if (command == Shortcut::delete_selection) {
            canvas.delete_selection();
            renderer.prune(canvas);
            inspector_visible = false;
            group_toolbar_visible = false;
        }
        if (command == Shortcut::copy || command == Shortcut::paste) {
            try {
                canvas.release();
                if (command == Shortcut::copy) copy_selection(canvas, window.handle.get());
                else {
                    double screen_x{}, screen_y{};
                    glfwGetCursorPos(window.handle.get(), &screen_x, &screen_y);
                    const auto [world_x, world_y] = canvas.world(screen_x, screen_y);
                    paste_selection(canvas, window.handle.get(), world_x, world_y);
                    inspector_visible = canvas.selected.size() == 1 && canvas.selected.front().kind == Selection::Kind::text;
                    group_toolbar_visible = !inspector_visible && !canvas.selected.empty();
                    redraw = true;
                }
            } catch (const std::exception& failure) {
                error = failure.what();
            }
        }
        const auto font_for = [&](const TextBlock& text) {
            return ui.text_fonts[static_cast<std::size_t>(text.weight)];
        };
        const auto measure = [&](TextBlock& text) {
            ImGui::PushFont(font_for(text), static_cast<float>(text.font_size / ui.scale));
            const auto size = ImGui::CalcTextSize(text.content.empty() ? " " : text.content.c_str(), nullptr, false, text.auto_width ? -1.0F : static_cast<float>(text.width));
            if (text.auto_width) text.width = std::max(static_cast<double>(text.font_size), static_cast<double>(size.x));
            const double next_line = text.content.ends_with('\n') ? ImGui::GetFontSize() : 0;
            text.height = std::max(static_cast<double>(text.font_size), size.y + next_line);
            ImGui::PopFont();
        };
        for (auto& text : canvas.texts) measure(text);

        if (!editing && pending == Action::none && error.empty() && !io.KeyAlt && !io.WantCaptureMouse && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const auto [world_x, world_y] = canvas.world(io.MousePos.x, io.MousePos.y);
            if (const auto hit = canvas.text_at(world_x, world_y)) start_edit(*hit, false);
            else if (canvas.selected.empty() || canvas.selected.front().kind == Selection::Kind::group) {
                canvas.release();
                const auto [create_x, create_y] = canvas.world(io.MousePos.x, io.MousePos.y);
                canvas.add_text(create_x, create_y);
                canvas.normalize();
                start_edit(canvas.texts.size() - 1, true);
            }
        }
        if (editing) {
            auto& text = canvas.texts[*editing];
            const auto [x, y] = canvas.screen(text.x, text.y);
            const ImVec2 size{static_cast<float>((text.width + (text.auto_width ? text.font_size : 0)) * canvas.zoom), static_cast<float>(text.height * canvas.zoom)};
            ImGui::SetNextWindowPos({static_cast<float>(x), static_cast<float>(y)});
            ImGui::SetNextWindowSize(size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::Begin("##TextEditor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(2);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{text.color[0] / 255.0F, text.color[1] / 255.0F, text.color[2] / 255.0F, 1});
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4{0, 0, 0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0F);
            ImGui::PushFont(font_for(text), static_cast<float>(text.font_size * canvas.zoom / ui.scale));
            if (focus_text && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImGui::SetKeyboardFocusHere();
                focus_text = false;
            }
            const ImGuiInputTextFlags flags = ImGuiInputTextFlags_NoHorizontalScroll | (text.auto_width ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_WordWrap);
            if (ImGui::InputTextMultiline("##Content", &text.content, size, flags)) {
                measure(text);
                redraw = true;
            }
            ImGui::PopFont();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(3);
            ImGui::End();
        }

        if (command == Shortcut::cancel_edit) finish_edit(false);
        if (command == Shortcut::save || command == Shortcut::close) finish_edit(true);
        if (command == Shortcut::fit) canvas.fit();
        if (command == Shortcut::close) request(Action::close);
        if (command == Shortcut::save) {
            try {
                save();
            } catch (const std::exception& failure) {
                error = failure.what();
            }
        }
        if (running && !dropped_files.empty() && pending == Action::none && error.empty()) {
            finish_edit(true);
            handle_drop();
        }

        auto* background = ImGui::GetBackgroundDrawList();
        for (std::size_t index = 0; index < canvas.texts.size(); ++index) {
            if (editing && *editing == index) continue;
            const auto& text = canvas.texts[index];
            const auto [x, y] = canvas.screen(text.x, text.y);
            const float width = static_cast<float>(text.width * canvas.zoom), height = static_cast<float>(text.height * canvas.zoom);
            if (x + width < 0 || y + height < 0 || x > io.DisplaySize.x || y > io.DisplaySize.y) continue;
            ImFont* font = font_for(text);
            ImGui::PushFont(font, static_cast<float>(text.font_size * canvas.zoom / ui.scale));
            const ImVec2 position{static_cast<float>(x), static_cast<float>(y)};
            const float font_size = ImGui::GetFontSize();
            const char* line = text.content.data();
            const char* end = line + text.content.size();
            float line_y = position.y;
            while (line < end) {
                const char* newline = std::find(line, end, '\n');
                const char* line_end = text.auto_width ? newline : font->CalcWordWrapPosition(font_size, line, newline, width);
                const float line_width = ImGui::CalcTextSize(line, line_end).x;
                const float remaining = std::max(0.0F, width - line_width);
                float line_x = position.x;
                if (text.alignment == TextBlock::Alignment::center) line_x += remaining / 2;
                if (text.alignment == TextBlock::Alignment::right) line_x += remaining;
                background->AddText(font, font_size, {line_x + ui.scale, line_y + ui.scale}, IM_COL32(0, 0, 0, 160), line, line_end);
                background->AddText(font, font_size, {line_x, line_y}, IM_COL32(text.color[0], text.color[1], text.color[2], 255), line, line_end);
                if (line_end == newline) line = newline < end ? newline + 1 : end;
                else {
                    line = line_end;
                    while (line < newline && (*line == ' ' || *line == '\t')) ++line;
                    if (line == newline && newline < end) ++line;
                }
                line_y += font_size;
            }
            ImGui::PopFont();
            if (std::ranges::find(canvas.selected, Selection{Selection::Kind::text, index}) != canvas.selected.end()) {
                background->AddRect({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(x) + width, static_cast<float>(y) + height}, IM_COL32(122, 139, 180, 220), 2 * ui.scale);
                if (canvas.selected.size() == 1) {
                    const ImVec2 handle{static_cast<float>(x) + width, static_cast<float>(y) + height / 2};
                    background->AddCircleFilled(handle, 4 * ui.scale, IM_COL32(174, 187, 218, 245));
                }
            }
        }

        if (canvas.marquee) {
            const auto [left, top] = canvas.screen((*canvas.marquee)[0], (*canvas.marquee)[1]);
            const auto [right, bottom] = canvas.screen((*canvas.marquee)[2], (*canvas.marquee)[3]);
            background->AddRectFilled({static_cast<float>(left), static_cast<float>(top)}, {static_cast<float>(right), static_cast<float>(bottom)}, IM_COL32(123, 146, 192, 25));
            background->AddRect({static_cast<float>(left), static_cast<float>(top)}, {static_cast<float>(right), static_cast<float>(bottom)}, IM_COL32(132, 156, 204, 185), 1.5F * ui.scale);
        }

        if (inspector_visible && !editing && canvas.selected.size() == 1 && canvas.selected.front().kind == Selection::Kind::text && pending == Action::none && error.empty()) {
            const std::size_t index = canvas.selected.front().index;
            auto& text = canvas.texts[index];
            const auto [screen_x, screen_y] = canvas.screen(text.x, text.y);
            const float width = 300 * ui.scale, height = 116 * ui.scale;
            const float x = std::clamp(static_cast<float>(screen_x + text.width * canvas.zoom / 2 - width / 2), 16 * ui.scale, std::max(16 * ui.scale, io.DisplaySize.x - width - 16 * ui.scale));
            const float above = static_cast<float>(screen_y) - height - 10 * ui.scale;
            const float below = static_cast<float>(screen_y + text.height * canvas.zoom) + 10 * ui.scale;
            const float y = std::clamp(above >= 16 * ui.scale ? above : below, 16 * ui.scale, std::max(16 * ui.scale, io.DisplaySize.y - height - 16 * ui.scale));
            inspector_bounds = {x, y, x + width, y + height};
            background->AddRectFilled({x, y + 3 * ui.scale}, {x + width, y + height + 3 * ui.scale}, IM_COL32(0, 0, 0, 35), 10 * ui.scale);
            ImGui::SetNextWindowPos({x, y});
            ImGui::SetNextWindowSize({width, height});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8 * ui.scale, 7 * ui.scale});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9 * ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{7 * ui.scale, 5 * ui.scale});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6 * ui.scale, 6 * ui.scale});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.12F, 0.13F, 0.16F, 0.98F});
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.32F, 0.34F, 0.39F, 0.8F});
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0.12F, 0.13F, 0.16F, 0.99F});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.18F, 0.19F, 0.23F, 1});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.22F, 0.23F, 0.28F, 1});
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.26F, 0.27F, 0.33F, 1});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.12F, 0.13F, 0.16F, 1});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.22F, 0.23F, 0.28F, 1});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.28F, 0.30F, 0.37F, 1});
            bool style_changed{}, delete_text{}, create_group{};
            ImGui::Begin("##TextInspector", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize);
            const bool clicked_canvas = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
                (io.MousePos.x < inspector_bounds[0] || io.MousePos.y < inspector_bounds[1] ||
                    io.MousePos.x > inspector_bounds[2] || io.MousePos.y > inspector_bounds[3]);
            constexpr std::array sizes{
                std::pair{"H1", 192},
                std::pair{"H2", 128},
                std::pair{"Body", 96},
                std::pair{"Caption", 64}
            };
            const float preset_width = (width - 28 * ui.scale) / 4;
            for (std::size_t option = 0; option < sizes.size(); ++option) {
                if (option) ImGui::SameLine(0, 4 * ui.scale);
                const auto& [name, size] = sizes[option];
                const bool selected = text.font_size == size;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.31F, 0.35F, 0.45F, 1});
                if (ImGui::Button(name, ImVec2{preset_width, 28 * ui.scale}) && !selected) {
                    text.font_size = size;
                    style_changed = true;
                }
                if (selected) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d canvas units", size);
            }
            ImGui::SetCursorPosX(27 * ui.scale);
            const bool bold = text.weight == TextBlock::Weight::bold;
            if (bold) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.31F, 0.35F, 0.45F, 1});
            if (ImGui::Button("B", ImVec2{30 * ui.scale, 0})) {
                text.weight = bold ? TextBlock::Weight::regular : TextBlock::Weight::bold;
                style_changed = true;
            }
            if (bold) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bold");
            ImGui::SameLine();
            if (ImGui::InvisibleButton("##Color", ImVec2{28 * ui.scale, 28 * ui.scale})) ImGui::OpenPopup("##TextColor");
            const ImVec2 color_center{(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2};
            if (ImGui::IsItemHovered()) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(58, 61, 72, 255), 6 * ui.scale);
                ImGui::SetTooltip("Text color");
            }
            ImGui::GetWindowDrawList()->AddCircleFilled(color_center, 7 * ui.scale, IM_COL32(text.color[0], text.color[1], text.color[2], 255));
            ImGui::GetWindowDrawList()->AddCircle(color_center, 7 * ui.scale, IM_COL32(17, 19, 25, 255));
            constexpr std::array hints{"Align left", "Align center", "Align right"};
            constexpr std::array strokes{14.0F, 9.0F, 12.0F, 7.0F};
            for (int option = 0; option < 3; ++option) {
                ImGui::SameLine(0, option == 0 ? 6 * ui.scale : 2 * ui.scale);
                ImGui::PushID(option);
                const bool selected = text.alignment == static_cast<TextBlock::Alignment>(option);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.31F, 0.35F, 0.45F, 1});
                if (ImGui::Button("##Alignment", ImVec2{26 * ui.scale, 28 * ui.scale})) {
                    text.alignment = static_cast<TextBlock::Alignment>(option);
                    style_changed = true;
                }
                if (selected) ImGui::PopStyleColor();
                const bool hovered = ImGui::IsItemHovered();
                const auto top_left = ImGui::GetItemRectMin();
                const ImU32 ink = selected || hovered ? IM_COL32(235, 238, 247, 255) : IM_COL32(151, 156, 172, 255);
                for (std::size_t row = 0; row < strokes.size(); ++row) {
                    const float length = strokes[row] * ui.scale;
                    float inset{};
                    if (option == 1) inset = (14 * ui.scale - length) / 2;
                    if (option == 2) inset = 14 * ui.scale - length;
                    const float left = top_left.x + 6 * ui.scale + inset;
                    const float line_y = top_left.y + (6 + row * 5) * ui.scale;
                    ImGui::GetWindowDrawList()->AddLine({left, line_y}, {left + length, line_y}, ink, 1.5F * ui.scale);
                }
                if (hovered) ImGui::SetTooltip("%s", hints[option]);
                ImGui::PopID();
            }
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6 * ui.scale, 8 * ui.scale});
            if (ImGui::BeginPopup("##TextColor")) {
                ImGui::TextDisabled("TEXT COLOR");
                constexpr std::array palette{
                    std::array<std::uint8_t, 3>{219, 221, 231},
                    std::array<std::uint8_t, 3>{250, 250, 250},
                    std::array<std::uint8_t, 3>{245, 204, 156},
                    std::array<std::uint8_t, 3>{241, 199, 91},
                    std::array<std::uint8_t, 3>{142, 222, 187},
                    std::array<std::uint8_t, 3>{139, 184, 240}
                };
                for (std::size_t choice = 0; choice < palette.size(); ++choice) {
                    ImGui::PushID(static_cast<int>(choice));
                    if (choice) ImGui::SameLine();
                    const auto& color = palette[choice];
                    if (ImGui::ColorButton("##Color", ImVec4{color[0] / 255.0F, color[1] / 255.0F, color[2] / 255.0F, 1}, ImGuiColorEditFlags_NoTooltip, ImVec2{24 * ui.scale, 24 * ui.scale})) {
                        text.color = color;
                        style_changed = true;
                    }
                    if (text.color == color) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(242, 243, 248, 255), 5 * ui.scale);
                    ImGui::PopID();
                }
                ImGui::Separator();
                float color[3]{text.color[0] / 255.0F, text.color[1] / 255.0F, text.color[2] / 255.0F};
                ImGui::SetNextItemWidth(210 * ui.scale);
                if (ImGui::ColorPicker3("##RGB", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
                    for (int channel = 0; channel < 3; ++channel) text.color[channel] = static_cast<std::uint8_t>(std::lround(color[channel] * 255));
                    style_changed = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.87F, 0.56F, 0.56F, 1});
            delete_text = ImGui::Button("Delete", ImVec2{54 * ui.scale, 0});
            ImGui::PopStyleColor();
            create_group = ImGui::Button("Group", ImVec2{(width - 22 * ui.scale), 25 * ui.scale});
            ImGui::End();
            ImGui::PopStyleColor(9);
            ImGui::PopStyleVar(7);
            if (clicked_canvas) {
                canvas.selected.clear();
                inspector_visible = false;
            }
            if (delete_text) {
                canvas.delete_selection();
                inspector_visible = false;
                redraw = true;
            } else if (style_changed) {
                measure(text);
                canvas.dirty = true;
                redraw = true;
            }
            if (create_group) {
                canvas.group_selection();
                inspector_visible = false;
                group_toolbar_visible = true;
            }
        }

        if (group_toolbar_visible && !editing && !canvas.selected.empty() && pending == Action::none && error.empty()) {
            const bool one_group = canvas.selected.size() == 1 && canvas.selected.front().kind == Selection::Kind::group;
            std::array<double, 4> box{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
            for (const auto item : canvas.selected) {
                const auto item_box = canvas.bounds(item);
                box[0] = std::min(box[0], item_box[0]);
                box[1] = std::min(box[1], item_box[1]);
                box[2] = std::max(box[2], item_box[2]);
                box[3] = std::max(box[3], item_box[3]);
            }
            const auto [left, top] = canvas.screen(box[0], box[1]);
            const auto [right, bottom] = canvas.screen(box[2], box[3]);
            const float width = (one_group ? 276.0F : 112.0F) * ui.scale;
            const float height = (one_group ? 76.0F : 46.0F) * ui.scale;
            const float x = std::clamp(static_cast<float>((left + right) / 2 - width / 2), 16 * ui.scale, std::max(16 * ui.scale, io.DisplaySize.x - width - 16 * ui.scale));
            const float above = static_cast<float>(top) - height - 10 * ui.scale;
            const float below = static_cast<float>(bottom) + 10 * ui.scale;
            const float y = std::clamp(above >= 16 * ui.scale ? above : below, 16 * ui.scale, std::max(16 * ui.scale, io.DisplaySize.y - height - 16 * ui.scale));
            group_toolbar_bounds = {x, y, x + width, y + height};
            background->AddRectFilled({x, y + 3 * ui.scale}, {x + width, y + height + 3 * ui.scale}, IM_COL32(0, 0, 0, 35), 10 * ui.scale);
            ImGui::SetNextWindowPos({x, y});
            ImGui::SetNextWindowSize({width, height});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {9 * ui.scale, 8 * ui.scale});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {7 * ui.scale, 6 * ui.scale});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * ui.scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {7 * ui.scale, 4 * ui.scale});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.12F, 0.13F, 0.16F, 0.98F});
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.32F, 0.34F, 0.39F, 0.8F});
            bool create_group{}, ungroup{}, background_changed{};
            std::optional<std::size_t> next_color;
            ImGui::Begin("##GroupToolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize);
            if (one_group) {
                auto& group = canvas.groups[canvas.selected.front().index];
                ImGui::TextDisabled("GROUP");
                ImGui::SameLine();
                for (std::size_t color = 0; color < Group::palette.size(); ++color) {
                    if (color) ImGui::SameLine(0, 5 * ui.scale);
                    bool conflict = group.parent && canvas.groups[static_cast<std::size_t>(std::ranges::find(canvas.groups, group.parent, &Group::id) - canvas.groups.begin())].color == color;
                    for (const auto& child : canvas.groups)
                        if (child.parent == group.id && child.color == color) conflict = true;
                    ImGui::BeginDisabled(conflict);
                    ImGui::PushID(static_cast<int>(color));
                    const auto& tint = Group::palette[color];
                    if (ImGui::ColorButton("##GroupColor", {tint[0], tint[1], tint[2], 1}, ImGuiColorEditFlags_NoTooltip, {23 * ui.scale, 23 * ui.scale})) next_color = color;
                    if (group.color == color) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(235, 238, 246, 255), 3 * ui.scale, 0, 1.5F * ui.scale);
                    ImGui::PopID();
                    ImGui::EndDisabled();
                }
                const float action_width = (width - 25 * ui.scale) / 2;
                background_changed = ImGui::Button(group.background ? "Background on" : "Background off", {action_width, 27 * ui.scale});
                ImGui::SameLine();
                ungroup = ImGui::Button("Ungroup", {action_width, 27 * ui.scale});
            } else create_group = ImGui::Button("Group", {92 * ui.scale, 27 * ui.scale});
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(6);
            if (one_group) {
                if (next_color) {
                    canvas.groups[canvas.selected.front().index].color = *next_color;
                    canvas.dirty = true;
                }
                if (background_changed) {
                    auto& group = canvas.groups[canvas.selected.front().index];
                    group.background = !group.background;
                    canvas.dirty = true;
                }
                if (ungroup) {
                    canvas.ungroup(canvas.selected.front().index);
                    group_toolbar_visible = false;
                }
            }
            if (create_group) canvas.group_selection();
        }
        if (canvas.pictures.empty() && canvas.texts.empty() && pending == Action::none && error.empty()) {
            constexpr auto title = "DROP PNG IMAGES";
            constexpr auto detail = "DOUBLE-CLICK TO ADD TEXT  \xC2\xB7  DROP A VISIA FILE TO OPEN";
            const auto center = ImVec2{io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F};
            const auto title_size = ImGui::CalcTextSize(title);
            const auto detail_size = ImGui::CalcTextSize(detail);
            background->AddText(ImVec2{center.x - title_size.x * 0.5F, center.y + 50 * ui.scale}, IM_COL32(150, 155, 173, 255), title);
            background->AddText(ImVec2{center.x - detail_size.x * 0.5F, center.y + 80 * ui.scale}, IM_COL32(89, 95, 110, 255), detail);
        }
        if ((!canvas.pictures.empty() || !canvas.texts.empty()) && pending == Action::none && error.empty()) {
            const double percent = canvas.zoom * 100;
            const auto label = percent < 1 ? std::format("Fit \xC2\xB7 {:.2f}%", percent) : std::format("Fit \xC2\xB7 {:.0f}%", percent);
            const auto text_size = ImGui::CalcTextSize(label.c_str());
            const ImVec2 size{std::max(104 * ui.scale, text_size.x + 24 * ui.scale), 40 * ui.scale};
            const ImVec2 origin{io.DisplaySize.x - size.x - 24 * ui.scale, io.DisplaySize.y - size.y - 24 * ui.scale};
            ImGui::SetNextWindowPos(origin);
            ImGui::SetNextWindowSize(size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            ImGui::Begin("##FitControl", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar();
            auto* draw = ImGui::GetWindowDrawList();
            const bool clicked = ImGui::InvisibleButton("##Fit", size);
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 text_position{origin.x + (size.x - text_size.x) / 2, origin.y + (size.y - text_size.y) / 2};
            draw->AddText({text_position.x, text_position.y + ui.scale}, IM_COL32(0, 0, 0, 179), label.c_str());
            const ImU32 fit_ink = hovered ? IM_COL32(225, 225, 237, 255) : IM_COL32(145, 148, 163, 255);
            draw->AddText(text_position, fit_ink, label.c_str());
            if (hovered) ImGui::SetTooltip("Left-click: 100%%\nMiddle-click or F: fit all content");
            if (clicked) {
                canvas.release();
                canvas.zoom = 1;
                canvas.dirty = true;
                redraw = true;
            }
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                canvas.fit();
                redraw = true;
            }
            ImGui::End();
        }
        enum class Decision { none, save, discard, cancel } decision{Decision::none};
        if (pending != Action::none) {
            if (ImGui::OpenPopup("Save changes?")) redraw = true;
            if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar)) {
                ImGui::TextUnformatted("Save changes?");
                ImGui::Spacing();
                ImGui::TextUnformatted("Save changes to this Visia document?");
                ImGui::Spacing();
                if (ImGui::Button("Save")) decision = Decision::save;
                ImGui::SameLine();
                if (ImGui::Button("Discard")) decision = Decision::discard;
                ImGui::SameLine();
                if (ImGui::Button("Cancel") || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsWindowAppearing())) decision = Decision::cancel;
                if (decision != Decision::none) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        if (!error.empty()) {
            if (ImGui::OpenPopup("Error")) redraw = true;
            if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar)) {
                ImGui::TextUnformatted("Error");
                ImGui::Spacing();
                ImGui::TextWrapped("%s", error.c_str());
                ImGui::Spacing();
                if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    ImGui::CloseCurrentPopup();
                    error.clear();
                    redraw = true;
                }
                ImGui::EndPopup();
            }
        }
        ImGui::Render();
        if (decision == Decision::none) return;
        const Action action = pending;
        pending = Action::none;
        try {
            if (decision == Decision::discard || (decision == Decision::save && save())) perform(action);
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        pending_document.clear();
        redraw = true;
    }

    void Application::loop() {
        auto* source = window.handle.get();
        while (running) {
            if (glfwWindowShouldClose(source)) {
                glfwSetWindowShouldClose(source, GLFW_FALSE);
                if (pending == Action::none && error.empty()) {
                    shortcut = Shortcut::close;
                    redraw = true;
                }
            }
            if (redraw) {
                redraw = false;
                draw_ui();
                if (!running) break;
                redraw = !renderer.draw(canvas) || redraw;
                if (redraw) continue;
            }
            if (editing) glfwWaitEventsTimeout(1.0 / 30);
            else glfwWaitEvents();
            if (editing) redraw = true;
        }
    }
}
