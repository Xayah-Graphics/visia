module;
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

module visia.renderer;
import visia.canvas;
import std;
import vulkan;

namespace visia {
    Renderer::Renderer(GLFWwindow* source) : window{source} {
        std::uint32_t extension_count{};
        const char** extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        const vk::ApplicationInfo application{"Visia", 1, "Visia", 1, vk::ApiVersion14};
        instance = vk::raii::Instance{context, vk::InstanceCreateInfo{{}, &application, 0, nullptr, extension_count, extensions}};
        VkSurfaceKHR raw_surface{};
        if (glfwCreateWindowSurface(*instance, window, nullptr, &raw_surface) != VK_SUCCESS) throw std::runtime_error{"Cannot create Vulkan surface"};
        surface = vk::raii::SurfaceKHR{instance, raw_surface};

        for (auto& candidate : instance.enumeratePhysicalDevices()) {
            if (candidate.getProperties().apiVersion < vk::ApiVersion14) continue;
            const auto families = candidate.getQueueFamilyProperties();
            for (std::uint32_t index = 0; index < families.size(); ++index) {
                if (!(families[index].queueFlags & vk::QueueFlagBits::eGraphics) || !candidate.getSurfaceSupportKHR(index, *surface)) continue;
                const auto formats = candidate.getSurfaceFormatsKHR(*surface);
                if (std::ranges::none_of(formats, [](const vk::SurfaceFormatKHR& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; })) continue;
                physical = std::move(candidate);
                family = index;
                break;
            }
            if (*physical) break;
        }
        if (!*physical) throw std::runtime_error{"Visia requires Vulkan 1.4 with graphics, present, and sRGB swapchain support"};
        memory = physical.getMemoryProperties();
        const auto texture_features = physical.getFormatProperties(vk::Format::eR8G8B8A8Srgb).optimalTilingFeatures;
        constexpr auto mip_features = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        if ((texture_features & mip_features) != mip_features) throw std::runtime_error{"Visia requires sRGB texture blitting"};
        const std::array priority{1.0F};
        const vk::DeviceQueueCreateInfo queue_info{{}, family, 1, priority.data()};
        vk::PhysicalDeviceVulkan13Features features13;
        features13.dynamicRendering = true;
        features13.synchronization2 = true;
        vk::PhysicalDeviceFeatures2 features2;
        features2.pNext = &features13;
        constexpr std::array device_extensions{vk::KHRSwapchainExtensionName};
        device = vk::raii::Device{physical, vk::DeviceCreateInfo{{}, 1, &queue_info, 0, nullptr, static_cast<std::uint32_t>(device_extensions.size()), device_extensions.data(), nullptr, &features2}};
        queue = device.getQueue(family, 0);

        command_pool = vk::raii::CommandPool{device, vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family}};
        commands = vk::raii::CommandBuffers{device, vk::CommandBufferAllocateInfo{*command_pool, vk::CommandBufferLevel::ePrimary, static_cast<std::uint32_t>(frames.size())}};
        for (auto& frame : frames) {
            frame.available = vk::raii::Semaphore{device, vk::SemaphoreCreateInfo{}};
            frame.finished = vk::raii::Fence{device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled}};
        }

        sampler = vk::raii::Sampler{device, vk::SamplerCreateInfo{{}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge}};
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment}};
        texture_layout = vk::raii::DescriptorSetLayout{device, vk::DescriptorSetLayoutCreateInfo{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()}};
        const std::array pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 256},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 256}};
        descriptor_pool = vk::raii::DescriptorPool{device, vk::DescriptorPoolCreateInfo{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 256, static_cast<std::uint32_t>(pool_sizes.size()), pool_sizes.data()}};
        static_assert(sizeof(PushData) == 64);
        const vk::PushConstantRange push{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushData)};
        grid_layout = vk::raii::PipelineLayout{device, vk::PipelineLayoutCreateInfo{{}, 0, nullptr, 1, &push}};
        const auto layout = *texture_layout;
        picture_layout = vk::raii::PipelineLayout{device, vk::PipelineLayoutCreateInfo{{}, 1, &layout, 1, &push}};
        shape_layout = vk::raii::PipelineLayout{device, vk::PipelineLayoutCreateInfo{{}, 0, nullptr, 1, &push}};
        grid_pipeline = pipeline("grid", grid_layout, false);
        picture_pipeline = pipeline("picture", picture_layout, true);
        shadow_pipeline = pipeline("shadow", shape_layout, true);
        shape_pipeline = pipeline("shape", shape_layout, true);
        group_pipeline = pipeline("group", shape_layout, true);
        recreate();
        constexpr VkFormat color_format = VK_FORMAT_B8G8R8A8_SRGB;
        ImGui_ImplVulkan_InitInfo ui{};
        ui.ApiVersion = VK_API_VERSION_1_4;
        ui.Instance = static_cast<VkInstance>(*instance);
        ui.PhysicalDevice = static_cast<VkPhysicalDevice>(*physical);
        ui.Device = static_cast<VkDevice>(*device);
        ui.QueueFamily = family;
        ui.Queue = static_cast<VkQueue>(*queue);
        ui.DescriptorPoolSize = 128;
        ui.MinImageCount = 2;
        ui.ImageCount = static_cast<std::uint32_t>(images.size());
        ui.UseDynamicRendering = true;
        ui.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ui.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        ui.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format;
        if (!ImGui_ImplVulkan_Init(&ui)) throw std::runtime_error{"Cannot initialize Visia UI renderer"};
        imgui_ready = true;
    }

    Renderer::~Renderer() {
        if (*device) device.waitIdle();
        if (imgui_ready) ImGui_ImplVulkan_Shutdown();
    }

    bool Renderer::draw(const Canvas& canvas) {
        int width{}, height{};
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) return true;
        if (extent.width != static_cast<std::uint32_t>(width) || extent.height != static_cast<std::uint32_t>(height)) recreate();
        auto& frame = frames[frame_index];
        static_cast<void>(device.waitForFences(*frame.finished, true, std::numeric_limits<std::uint64_t>::max()));
        frame.staging.clear();
        std::uint32_t image_index{};
        try {
            image_index = swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *frame.available).value;
        } catch (const vk::OutOfDateKHRError&) {
            recreate();
            return false;
        }
        device.resetFences(*frame.finished);
        const auto& command = commands[frame_index];
        command.reset();
        command.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        ++frame_number;
        const float dpi_x = static_cast<float>(extent.width / canvas.viewport_width);
        const float dpi_y = static_cast<float>(extent.height / canvas.viewport_height);
        for (const auto& picture : canvas.pictures) {
            const auto [left, top] = canvas.screen(picture.x, picture.y);
            const double right = left + picture.width * picture.scale * canvas.zoom;
            const double bottom = top + picture.height * picture.scale * canvas.zoom;
            if (right < 0 || bottom < 0 || left > canvas.viewport_width || top > canvas.viewport_height) continue;
            const double resolution = std::min(1.0, std::max(64.0 / std::max(picture.width, picture.height), 2 * picture.scale * canvas.zoom * std::max(dpi_x, dpi_y)));
            const int target_width = std::max(1, static_cast<int>(std::ceil(picture.width * resolution)));
            const int target_height = std::max(1, static_cast<int>(std::ceil(picture.height * resolution)));
            if (const auto cached = textures.find(picture.id); cached != textures.end() && (cached->second.width < target_width || cached->second.height < target_height)) {
                device.waitIdle();
                textures.erase(cached);
            }
            if (!textures.contains(picture.id)) upload(picture, target_width, target_height, command, frame);
            textures.at(picture.id).last_used = frame_number;
        }
        const vk::ImageMemoryBarrier2 attachment{vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, images[image_index], {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, 1, &attachment});
        const vk::RenderingAttachmentInfo color{*views[image_index], vk::ImageLayout::eColorAttachmentOptimal, {}, {}, {}, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearValue{vk::ClearColorValue{std::array{0.009F, 0.011F, 0.016F, 1.0F}}}};
        command.beginRendering(vk::RenderingInfo{{}, {{0, 0}, extent}, 1, 0, 1, &color});
        command.setViewport(0, vk::Viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1});
        command.setScissor(0, vk::Rect2D{{0, 0}, extent});

        PushData push;
        push.viewport = {static_cast<float>(extent.width), static_cast<float>(extent.height)};
        const double spacing = Canvas::grid * canvas.zoom * dpi_x;
        push.spacing = static_cast<float>(spacing);
        push.grid_opacity = static_cast<float>(std::clamp((spacing - 2) / 22, 0.0, 1.0));
        const auto [anchor_x, anchor_y] = canvas.screen(0, 0);
        push.phase = {static_cast<float>(std::remainder(anchor_x * dpi_x, spacing)), static_cast<float>(std::remainder(anchor_y * dpi_y, spacing))};
        constexpr auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, *grid_pipeline);
        command.pushConstants(*grid_layout, stages, 0, sizeof(PushData), &push);
        command.draw(3, 1, 0, 0);

        const auto draw_groups = [&](auto&& self, const std::uint64_t parent, const bool fill) -> void {
            for (std::size_t i = 0; i < canvas.groups.size(); ++i) {
                const auto& group = canvas.groups[i];
                if (group.parent != parent) continue;
                const bool target = group.id == canvas.drop_target;
                if (!fill || group.background || target) {
                    const auto box = canvas.bounds({Selection::Kind::group, i});
                    const auto [left, top] = canvas.screen(box[0], box[1]);
                    push.rect = {static_cast<float>(left * dpi_x), static_cast<float>(top * dpi_y),
                        static_cast<float>((box[2] - box[0]) * canvas.zoom * dpi_x),
                        static_cast<float>((box[3] - box[1]) * canvas.zoom * dpi_y)};
                    const auto& tint = Group::palette[group.color];
                    const bool emphasized = !fill && std::ranges::find(canvas.selected, Selection{Selection::Kind::group, i}) != canvas.selected.end();
                    push.color = {tint[0], tint[1], tint[2], fill ? target ? 0.17F : 0.10F : target ? 1.0F : emphasized ? 0.98F : 0.82F};
                    push.group_style = {9 * dpi_x, fill ? 0.0F : target ? 4.0F * dpi_x : emphasized ? 2.5F * dpi_x : 1.5F * dpi_x};
                    command.bindPipeline(vk::PipelineBindPoint::eGraphics, *group_pipeline);
                    command.pushConstants(*shape_layout, stages, 0, sizeof(PushData), &push);
                    command.draw(6, 1, 0, 0);
                }
                self(self, group.id, fill);
            }
        };
        draw_groups(draw_groups, 0, true);

        for (const auto& picture : canvas.pictures) {
            const auto texture = textures.find(picture.id);
            if (texture == textures.end() || texture->second.last_used != frame_number) continue;
            const auto [left, top] = canvas.screen(picture.x, picture.y);
            const float x = static_cast<float>(left * dpi_x), y = static_cast<float>(top * dpi_y);
            const float w = static_cast<float>(picture.width * picture.scale * canvas.zoom * dpi_x);
            const float h = static_cast<float>(picture.height * picture.scale * canvas.zoom * dpi_y);
            push.rect = {x - 12, y - 12, w + 24, h + 24};
            command.bindPipeline(vk::PipelineBindPoint::eGraphics, *shadow_pipeline);
            command.pushConstants(*shape_layout, stages, 0, sizeof(PushData), &push);
            command.draw(6, 1, 0, 0);
            push.rect = {x, y, w, h};
            command.bindPipeline(vk::PipelineBindPoint::eGraphics, *picture_pipeline);
            const auto descriptor = *texture->second.descriptor;
            command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *picture_layout, 0, descriptor, {});
            command.pushConstants(*picture_layout, stages, 0, sizeof(PushData), &push);
            command.draw(6, 1, 0, 0);
        }

        draw_groups(draw_groups, 0, false);
        for (const auto item : canvas.selected) {
            if (item.kind != Selection::Kind::picture) continue;
            const auto& picture = canvas.pictures[item.index];
            const auto [left, top] = canvas.screen(picture.x, picture.y);
            const float x = static_cast<float>(left * dpi_x), y = static_cast<float>(top * dpi_y);
            const float w = static_cast<float>(picture.width * picture.scale * canvas.zoom * dpi_x);
            const float h = static_cast<float>(picture.height * picture.scale * canvas.zoom * dpi_y);
            push.color = {0.39F, 0.39F, 0.62F, 0.85F};
            command.bindPipeline(vk::PipelineBindPoint::eGraphics, *shape_pipeline);
            const auto rectangle = [&](const float rx, const float ry, const float rw, const float rh) {
                push.rect = {rx, ry, rw, rh};
                command.pushConstants(*shape_layout, stages, 0, sizeof(PushData), &push);
                command.draw(6, 1, 0, 0);
            };
            rectangle(x, y, w, 1.25F);
            rectangle(x, y + h - 1.25F, w, 1.25F);
            rectangle(x, y, 1.25F, h);
            rectangle(x + w - 1.25F, y, 1.25F, h);
            if (canvas.selected.size() == 1) {
                rectangle(x - 2, y - 2, 5, 5);
                push.color = {0.008F, 0.010F, 0.015F, 1};
                rectangle(x + w - 5, y + h - 5, 10, 10);
                push.color = {0.39F, 0.39F, 0.62F, 0.95F};
                rectangle(x + w - 5, y + h - 5, 10, 1);
                rectangle(x + w - 5, y + h + 4, 10, 1);
                rectangle(x + w - 5, y + h - 5, 1, 10);
                rectangle(x + w + 4, y + h - 5, 1, 10);
            }
        }
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(*command));
        command.endRendering();
        const vk::ImageMemoryBarrier2 present{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eNone, {}, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, images[image_index], {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, 1, &present});
        command.end();
        const vk::SemaphoreSubmitInfo wait{*frame.available, 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput};
        const vk::SemaphoreSubmitInfo signal{*presented[image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::CommandBufferSubmitInfo submitted{*command};
        queue.submit2(vk::SubmitInfo2{{}, 1, &wait, 1, &submitted, 1, &signal}, *frame.finished);
        const auto semaphore = *presented[image_index];
        const auto chain = *swapchain;
        try {
            static_cast<void>(queue.presentKHR(vk::PresentInfoKHR{1, &semaphore, 1, &chain, &image_index}));
        } catch (const vk::OutOfDateKHRError&) {
            recreate();
            frame_index = (frame_index + 1) % frames.size();
            return false;
        }
        frame_index = (frame_index + 1) % frames.size();
        return true;
    }

    void Renderer::clear() {
        device.waitIdle();
        textures.clear();
        for (auto& frame : frames) frame.staging.clear();
    }

    void Renderer::prune(const Canvas& canvas) {
        const auto obsolete = [&](const auto& entry) {
            return std::ranges::find(canvas.pictures, entry.first, &Picture::id) == canvas.pictures.end();
        };
        if (std::ranges::none_of(textures, obsolete)) return;
        device.waitIdle();
        std::erase_if(textures, obsolete);
    }

    void Renderer::recreate() {
        device.waitIdle();
        int width{}, height{};
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) return;
        const auto capabilities = physical.getSurfaceCapabilitiesKHR(*surface);
        const vk::Extent2D desired{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        extent = capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ? desired : capabilities.currentExtent;
        const std::uint32_t count = std::max(2u, capabilities.minImageCount);
        const vk::SwapchainCreateInfoKHR info{{}, *surface, capabilities.maxImageCount ? std::min(count, capabilities.maxImageCount) : count, vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear, extent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, 0, nullptr, capabilities.currentTransform, vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eFifo, true, *swapchain};
        vk::raii::SwapchainKHR replacement{device, info};
        views.clear();
        presented.clear();
        swapchain = std::move(replacement);
        images = swapchain.getImages();
        for (const auto image : images) {
            views.emplace_back(device, vk::ImageViewCreateInfo{{}, image, vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Srgb, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            presented.emplace_back(device, vk::SemaphoreCreateInfo{});
        }
        if (imgui_ready) ImGui_ImplVulkan_SetMinImageCount(static_cast<std::uint32_t>(images.size()));
    }

    std::uint32_t Renderer::memory_type(const std::uint32_t bits, const vk::MemoryPropertyFlags properties) const {
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index)
            if ((bits & (1u << index)) && (memory.memoryTypes[index].propertyFlags & properties) == properties) return index;
        throw std::runtime_error{"Required Vulkan memory type is unavailable"};
    }

    vk::raii::Pipeline Renderer::pipeline(const std::string_view name, const vk::raii::PipelineLayout& layout, const bool blend) const {
        std::array<std::vector<std::uint32_t>, 2> code;
        std::array<vk::raii::ShaderModule, 2> modules{vk::raii::ShaderModule{nullptr}, vk::raii::ShaderModule{nullptr}};
        std::array<vk::PipelineShaderStageCreateInfo, 2> stages;
        std::array<std::string, 2> entries;
        for (std::size_t index = 0; index < 2; ++index) {
            const auto stage = index == 0 ? "vertex" : "fragment";
            const auto path = std::filesystem::path{VISIA_SHADER_DIRECTORY} / std::format("{}_{}.spv", name, stage);
            std::ifstream input{path, std::ios::binary | std::ios::ate};
            input.exceptions(std::ios::badbit | std::ios::failbit);
            code[index].resize(static_cast<size_t>(input.tellg()) / 4);
            input.seekg(0);
            input.read(reinterpret_cast<char*>(code[index].data()), static_cast<std::streamsize>(code[index].size() * 4));
            modules[index] = vk::raii::ShaderModule{device, vk::ShaderModuleCreateInfo{{}, code[index].size() * 4, code[index].data()}};
            entries[index] = std::format("{}_{}", name, stage);
            stages[index] = vk::PipelineShaderStageCreateInfo{{}, index == 0 ? vk::ShaderStageFlagBits::eVertex : vk::ShaderStageFlagBits::eFragment, *modules[index], entries[index].c_str()};
        }
        const vk::PipelineVertexInputStateCreateInfo vertex;
        const vk::PipelineInputAssemblyStateCreateInfo assembly{{}, vk::PrimitiveTopology::eTriangleList};
        const vk::PipelineViewportStateCreateInfo viewport{{}, 1, nullptr, 1, nullptr};
        const vk::PipelineRasterizationStateCreateInfo raster{{}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, false, 0, 0, 0, 1};
        const vk::PipelineMultisampleStateCreateInfo samples{{}, vk::SampleCountFlagBits::e1};
        const vk::PipelineColorBlendAttachmentState attachment{blend, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
        const vk::PipelineColorBlendStateCreateInfo blending{{}, false, vk::LogicOp::eCopy, 1, &attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        constexpr vk::Format format = vk::Format::eB8G8R8A8Srgb;
        const vk::PipelineRenderingCreateInfo rendering{0, 1, &format};
        const vk::GraphicsPipelineCreateInfo info{{}, static_cast<std::uint32_t>(stages.size()), stages.data(), &vertex, &assembly, nullptr, &viewport, &raster, &samples, nullptr, &blending, &dynamic, *layout, nullptr, 0, nullptr, -1, &rendering};
        return vk::raii::Pipeline{device, nullptr, info};
    }

    void Renderer::upload(const Picture& picture, const int target_width, const int target_height, const vk::raii::CommandBuffer& command, Frame& frame) {
        int width{}, height{}, components{};
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{stbi_load_from_memory(picture.png.data(), static_cast<int>(picture.png.size()), &width, &height, &components, 4), stbi_image_free};
        if (!pixels) throw std::runtime_error{"Cannot decode embedded PNG"};
        std::vector<std::uint8_t> reduced;
        const std::uint8_t* source = pixels.get();
        if (target_width != width || target_height != height) {
            reduced.resize(static_cast<std::size_t>(target_width) * target_height * 4);
            if (!stbir_resize_uint8_srgb(pixels.get(), width, height, 0, reduced.data(), target_width, target_height, 0, STBIR_RGBA)) throw std::runtime_error{"Cannot resize embedded PNG"};
            source = reduced.data();
        }
        width = target_width;
        height = target_height;
        const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
        auto& staging = frame.staging.emplace_back();
        staging.buffer = vk::raii::Buffer{device, vk::BufferCreateInfo{{}, bytes, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive}};
        const auto staging_requirements = staging.buffer.getMemoryRequirements();
        staging.memory = vk::raii::DeviceMemory{device, vk::MemoryAllocateInfo{staging_requirements.size, memory_type(staging_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
        staging.buffer.bindMemory(*staging.memory, 0);
        void* mapped = staging.memory.mapMemory(0, bytes);
        std::memcpy(mapped, source, bytes);
        staging.memory.unmapMemory();

        Texture texture;
        texture.bytes = bytes;
        texture.width = width;
        texture.height = height;
        const std::uint32_t levels = std::bit_width(static_cast<std::uint32_t>(std::max(width, height)));
        texture.image = vk::raii::Image{device, vk::ImageCreateInfo{{}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Srgb, {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1}, levels, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive}};
        const auto requirements = texture.image.getMemoryRequirements();
        texture.memory = vk::raii::DeviceMemory{device, vk::MemoryAllocateInfo{requirements.size, memory_type(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
        texture.image.bindMemory(*texture.memory, 0);
        texture.view = vk::raii::ImageView{device, vk::ImageViewCreateInfo{{}, *texture.image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Srgb, {}, {vk::ImageAspectFlagBits::eColor, 0, levels, 0, 1}}};

        const auto layout = *texture_layout;
        vk::raii::DescriptorSets allocated{device, vk::DescriptorSetAllocateInfo{*descriptor_pool, 1, &layout}};
        texture.descriptor = std::move(allocated[0]);
        const vk::DescriptorImageInfo image_info{{}, *texture.view, vk::ImageLayout::eShaderReadOnlyOptimal};
        const vk::DescriptorImageInfo sampler_info{*sampler, {}, vk::ImageLayout::eUndefined};
        const std::array writes{
            vk::WriteDescriptorSet{*texture.descriptor, 0, 0, 1, vk::DescriptorType::eSampledImage, &image_info},
            vk::WriteDescriptorSet{*texture.descriptor, 1, 0, 1, vk::DescriptorType::eSampler, &sampler_info}};
        device.updateDescriptorSets(writes, {});

        const auto transition = [&](const std::uint32_t level, const vk::ImageLayout before, const vk::ImageLayout after, const vk::PipelineStageFlags2 source_stage, const vk::AccessFlags2 source_access, const vk::PipelineStageFlags2 target_stage, const vk::AccessFlags2 target_access) {
            const vk::ImageMemoryBarrier2 barrier{source_stage, source_access, target_stage, target_access, before, after, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *texture.image, {vk::ImageAspectFlagBits::eColor, level, 1, 0, 1}};
            command.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, 1, &barrier});
        };
        for (std::uint32_t level = 0; level < levels; ++level) transition(level, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
        command.copyBufferToImage(*staging.buffer, *texture.image, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1}});
        for (std::uint32_t level = 1; level < levels; ++level) {
            transition(level - 1, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, level == 1 ? vk::PipelineStageFlagBits2::eCopy : vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead);
            vk::ImageBlit region;
            region.srcSubresource = {vk::ImageAspectFlagBits::eColor, level - 1, 0, 1};
            region.srcOffsets[1] = vk::Offset3D{std::max(1, width >> (level - 1)), std::max(1, height >> (level - 1)), 1};
            region.dstSubresource = {vk::ImageAspectFlagBits::eColor, level, 0, 1};
            region.dstOffsets[1] = vk::Offset3D{std::max(1, width >> level), std::max(1, height >> level), 1};
            command.blitImage(*texture.image, vk::ImageLayout::eTransferSrcOptimal, *texture.image, vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eLinear);
            transition(level - 1, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        }
        transition(levels - 1, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        textures.emplace(picture.id, std::move(texture));
    }
}
