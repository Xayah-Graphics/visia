module;
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

module visia.app;
import visia.canvas;
import visia.document;
import visia.renderer;
import visia.window;
import std;

namespace visia {
    Application::UiLifetime::UiLifetime() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/SegUIVar.ttf");
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
            if (app.pending == Action::none && app.error.empty() && !ImGui::GetIO().WantCaptureMouse) app.canvas.move(x, y);
            app.redraw = true;
        });
        glfwSetMouseButtonCallback(source, [](GLFWwindow* source, const int button, const int action, int) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.redraw = true;
            if (action == GLFW_RELEASE) {
                app.canvas.release();
                return;
            }
            if (app.pending != Action::none || !app.error.empty() || ImGui::GetIO().WantCaptureMouse) return;
            double x{}, y{};
            glfwGetCursorPos(source, &x, &y);
            if (action == GLFW_PRESS) app.canvas.press(x, y, button);
        });
        glfwSetScrollCallback(source, [](GLFWwindow* source, double, const double steps) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.redraw = true;
            if (app.pending != Action::none || !app.error.empty() || ImGui::GetIO().WantCaptureMouse) return;
            double x{}, y{};
            glfwGetCursorPos(source, &x, &y);
            app.canvas.wheel(x, y, steps);
        });
        glfwSetDropCallback(source, [](GLFWwindow* source, const int count, const char** paths) {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            if (app.pending != Action::none || !app.error.empty()) return;
            try {
                std::vector<std::filesystem::path> files;
                files.reserve(count);
                for (int index = 0; index < count; ++index) files.emplace_back(std::u8string{reinterpret_cast<const char8_t*>(paths[index])});
                const auto document = std::ranges::find_if(files, [](const std::filesystem::path& file) {
                    auto extension = file.extension().wstring();
                    for (auto& letter : extension) if (letter >= L'A' && letter <= L'Z') letter += L'a' - L'A';
                    return extension == L".visia";
                });
                if (document != files.end()) {
                    if (files.size() != 1) throw std::runtime_error{"Drop one .visia document at a time, without PNG images"};
                    app.request(Action::open, *document);
                    return;
                }
                std::vector<Picture> pictures;
                for (const auto& file : files) pictures.push_back(read_png(file));
                double x{}, y{};
                glfwGetCursorPos(source, &x, &y);
                auto position = app.canvas.world(x, y);
                for (auto& picture : pictures) {
                    app.canvas.add(std::move(picture.png), picture.width, picture.height, position[0], position[1]);
                    const auto& placed = app.canvas.pictures.back();
                    position[0] = Canvas::snap(placed.x + placed.width * placed.scale) + Canvas::grid;
                }
                app.canvas.normalize();
                app.redraw = true;
            } catch (const std::exception& failure) {
                app.error = failure.what();
                app.redraw = true;
            }
        });
        glfwSetKeyCallback(source, [](GLFWwindow* source, const int key, int, const int action, const int modifiers) {
            if (action != GLFW_PRESS) return;
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(source));
            app.redraw = true;
            if (key == GLFW_KEY_ESCAPE && !(modifiers & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER)) && app.pending == Action::none && app.error.empty()) {
                app.request(Action::close);
                return;
            }
            if (key == GLFW_KEY_F && !(modifiers & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER)) && app.pending == Action::none && app.error.empty()) {
                app.canvas.fit();
                return;
            }
            if (app.pending != Action::none || !app.error.empty() || key != GLFW_KEY_S || !(modifiers & GLFW_MOD_CONTROL) || (modifiers & (GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER))) return;
            try {
                app.save();
            } catch (const std::exception& failure) {
                app.error = failure.what();
            }
        });
        ui.attach(source);
        glfwShowWindow(source);
    }

    void Application::load(const std::filesystem::path& path) {
        Canvas opened = open_document(path);
        renderer.clear();
        canvas = std::move(opened);
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
            std::wstring executable(32768, L'\0');
            executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
            const auto directory = std::filesystem::path{executable}.parent_path().u8string();
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
        ui.begin(window.handle.get());
        const auto& io = ImGui::GetIO();
        if (canvas.pictures.empty() && pending == Action::none && error.empty()) {
            constexpr auto title = "DROP PNG IMAGES";
            constexpr auto detail = "DROP A VISIA FILE TO OPEN IT";
            const auto center = ImVec2{io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F};
            const auto title_size = ImGui::CalcTextSize(title);
            const auto detail_size = ImGui::CalcTextSize(detail);
            auto* background = ImGui::GetBackgroundDrawList();
            background->AddText(ImVec2{center.x - title_size.x * 0.5F, center.y + 50 * ui.scale}, IM_COL32(150, 155, 173, 255), title);
            background->AddText(ImVec2{center.x - detail_size.x * 0.5F, center.y + 80 * ui.scale}, IM_COL32(89, 95, 110, 255), detail);
        }
        if (!canvas.pictures.empty() && pending == Action::none && error.empty()) {
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
            draw->AddText(text_position, hovered ? IM_COL32(225, 225, 237, 255) : IM_COL32(145, 148, 163, 255), label.c_str());
            if (hovered) ImGui::SetTooltip("Left-click: 100%%\nMiddle-click or F: fit all images");
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
                if (pending == Action::none && error.empty()) request(Action::close);
            }
            if (redraw) {
                redraw = false;
                draw_ui();
                if (!running) break;
                redraw = !renderer.draw(canvas) || redraw;
                if (redraw) continue;
            }
            glfwWaitEvents();
        }
    }
}
