module;
#include <GLFW/glfw3.h>

export module visia.renderer;
import visia.canvas;
import std;
import vulkan;

export namespace visia {
    struct Renderer {
        explicit Renderer(GLFWwindow* window);
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        [[nodiscard]] bool draw(const Canvas& canvas);
        void clear();

    private:
        struct Buffer {
            vk::raii::DeviceMemory memory{nullptr};
            vk::raii::Buffer buffer{nullptr};
        };

        struct Texture {
            vk::raii::DeviceMemory memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView view{nullptr};
            vk::raii::DescriptorSet descriptor{nullptr};
            std::size_t bytes{};
            std::uint64_t last_used{};
            int width{}, height{};
        };

        struct Frame {
            vk::raii::Semaphore available{nullptr};
            vk::raii::Fence finished{nullptr};
            std::vector<Buffer> staging;
        };

        struct PushData {
            std::array<float, 2> viewport{};
            std::array<float, 2> phase{};
            float spacing{}, grid_opacity{};
            std::array<float, 4> rect{};
            std::array<float, 4> color{};
        };

        GLFWwindow* window{};
        vk::raii::Context context;
        vk::raii::Instance instance{nullptr};
        vk::raii::SurfaceKHR surface{nullptr};
        vk::raii::PhysicalDevice physical{nullptr};
        vk::raii::Device device{nullptr};
        vk::raii::Queue queue{nullptr};
        vk::PhysicalDeviceMemoryProperties memory;
        std::uint32_t family{};
        vk::Extent2D extent{};
        vk::raii::CommandPool command_pool{nullptr};
        vk::raii::CommandBuffers commands{nullptr};
        std::array<Frame, 2> frames;
        vk::raii::SwapchainKHR swapchain{nullptr};
        std::vector<vk::Image> images;
        std::vector<vk::raii::ImageView> views;
        std::vector<vk::raii::Semaphore> presented;
        vk::raii::Sampler sampler{nullptr};
        vk::raii::DescriptorSetLayout texture_layout{nullptr};
        vk::raii::DescriptorPool descriptor_pool{nullptr};
        vk::raii::PipelineLayout grid_layout{nullptr};
        vk::raii::PipelineLayout picture_layout{nullptr};
        vk::raii::PipelineLayout shape_layout{nullptr};
        vk::raii::Pipeline grid_pipeline{nullptr};
        vk::raii::Pipeline picture_pipeline{nullptr};
        vk::raii::Pipeline shadow_pipeline{nullptr};
        vk::raii::Pipeline shape_pipeline{nullptr};
        std::map<std::uint64_t, Texture> textures;
        std::size_t frame_index{};
        std::uint64_t frame_number{};
        bool imgui_ready{};

        void recreate();
        [[nodiscard]] std::uint32_t memory_type(std::uint32_t bits, vk::MemoryPropertyFlags properties) const;
        [[nodiscard]] vk::raii::Pipeline pipeline(std::string_view name, const vk::raii::PipelineLayout& layout, bool blend) const;
        void upload(const Picture& picture, int target_width, int target_height, const vk::raii::CommandBuffer& command, Frame& frame);
    };
}
