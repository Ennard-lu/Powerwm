// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/scene_renderer.hpp>

#include <array>
#include <chrono>
#include <utility>
#include <vulkan/vulkan.h>

namespace pwm::render {

namespace {

constexpr u32 max_textures = 64;

struct draw_constants {
    f32 viewport[2]{};
    f32 padding[2]{};
    f32 rect[4]{};
    f32 color[4]{};
    f32 rotation = 0.0F;
    f32 scale = 1.0F;
    f32 corner_radius = 0.0F;
    f32 shadow_radius = 0.0F;
    f32 blur_radius = 0.0F;
    f32 opacity = 1.0F;
    i32 texture_index = -1;
    i32 is_shadow = 0;
};

static_assert(sizeof(draw_constants) == 80);

auto to_vk_format(target_format format) -> VkFormat {
    return format == target_format::bgra8 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
}

auto create_render_pass(VkDevice device, VkFormat format, bool load) -> result<VkRenderPass> {
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = load ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentReference reference{};
    reference.attachment = 0;
    reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    const VkResult result = vkCreateRenderPass(device, &create_info, nullptr, &render_pass);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateRenderPass failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return render_pass;
}

auto create_descriptor_layout(VkDevice device) -> result<VkDescriptorSetLayout> {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = max_textures;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 1;
    create_info.pBindings = &binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult result = vkCreateDescriptorSetLayout(device, &create_info, nullptr, &layout);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateDescriptorSetLayout failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return layout;
}

auto create_descriptor_pool(VkDevice device) -> result<VkDescriptorPool> {
    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = max_textures;

    VkDescriptorPoolCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.maxSets = 1;
    create_info.poolSizeCount = 1;
    create_info.pPoolSizes = &size;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    const VkResult result = vkCreateDescriptorPool(device, &create_info, nullptr, &pool);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateDescriptorPool failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return pool;
}

auto allocate_descriptor_set(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout)
    -> result<VkDescriptorSet> {
    VkDescriptorSetAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool = pool;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = vkAllocateDescriptorSets(device, &allocate_info, &set);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkAllocateDescriptorSets failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return set;
}

auto create_shader_module(VkDevice device, std::span<const std::byte> spirv)
    -> result<VkShaderModule> {
    if (spirv.size() % 4 != 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "SPIR-V size is not aligned"));
    }

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = spirv.size();
    create_info.pCode = reinterpret_cast<const u32*>(spirv.data());

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &module);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateShaderModule failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return module;
}

auto create_graphics_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout,
                              VkShaderModule vertex_module, VkShaderModule fragment_module)
    -> result<VkPipeline> {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_module;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount = 2;
    create_info.pStages = stages;
    create_info.pVertexInputState = &vertex_input;
    create_info.pInputAssemblyState = &input_assembly;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &rasterization;
    create_info.pMultisampleState = &multisample;
    create_info.pColorBlendState = &color_blend;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = render_pass;
    create_info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateGraphicsPipelines failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return pipeline;
}

auto create_pipeline(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout,
                     std::span<const std::byte> vertex_spirv,
                     std::span<const std::byte> fragment_spirv) -> result<VkPipeline> {
    auto vertex_result = create_shader_module(device, vertex_spirv);
    if (!vertex_result) {
        return std::unexpected(vertex_result.error());
    }
    auto fragment_result = create_shader_module(device, fragment_spirv);
    if (!fragment_result) {
        vkDestroyShaderModule(device, *vertex_result, nullptr);
        return std::unexpected(fragment_result.error());
    }

    auto pipeline_result =
        create_graphics_pipeline(device, render_pass, layout, *vertex_result, *fragment_result);
    vkDestroyShaderModule(device, *vertex_result, nullptr);
    vkDestroyShaderModule(device, *fragment_result, nullptr);
    return pipeline_result;
}

auto create_view(VkDevice device, VkImage image, VkFormat format) -> result<VkImageView> {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = format;
    create_info.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkImageView view = VK_NULL_HANDLE;
    const VkResult result = vkCreateImageView(device, &create_info, nullptr, &view);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImageView failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return view;
}

auto create_framebuffer(VkDevice device, VkRenderPass render_pass, VkImageView view, u32 width_px,
                        u32 height_px) -> result<VkFramebuffer> {
    VkFramebufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create_info.renderPass = render_pass;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &view;
    create_info.width = width_px;
    create_info.height = height_px;
    create_info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult result = vkCreateFramebuffer(device, &create_info, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateFramebuffer failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return framebuffer;
}

auto destroy_target(const vulkanpp::device& device, scene_renderer::target_entry& target) -> void {
    const VkDevice native = reinterpret_cast<VkDevice>(device.native_handle());
    if (target.framebuffer_clear != nullptr) {
        vkDestroyFramebuffer(native, reinterpret_cast<VkFramebuffer>(target.framebuffer_clear),
                             nullptr);
    }
    if (target.framebuffer_load != nullptr) {
        vkDestroyFramebuffer(native, reinterpret_cast<VkFramebuffer>(target.framebuffer_load),
                             nullptr);
    }
    if (target.view != nullptr) {
        vkDestroyImageView(native, reinterpret_cast<VkImageView>(target.view), nullptr);
    }
    target = {};
}

struct pipeline_resources {
    VkRenderPass clear_pass = VK_NULL_HANDLE;
    VkRenderPass load_pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

auto destroy_pipeline_resources(VkDevice device, pipeline_resources& resources) -> void {
    if (resources.descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, resources.descriptor_pool, nullptr);
    }
    if (resources.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, resources.pipeline, nullptr);
    }
    if (resources.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, resources.pipeline_layout, nullptr);
    }
    if (resources.descriptor_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, resources.descriptor_layout, nullptr);
    }
    if (resources.load_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, resources.load_pass, nullptr);
    }
    if (resources.clear_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, resources.clear_pass, nullptr);
    }
    resources = {};
}

auto build_render_passes(VkDevice device, VkFormat format, pipeline_resources& resources)
    -> result<void> {
    auto clear_result = create_render_pass(device, format, false);
    if (!clear_result) {
        return std::unexpected(clear_result.error());
    }
    resources.clear_pass = *clear_result;

    auto load_result = create_render_pass(device, format, true);
    if (!load_result) {
        return std::unexpected(load_result.error());
    }
    resources.load_pass = *load_result;
    return {};
}

auto build_descriptor_pipeline(VkDevice device, std::span<const std::byte> vertex_spirv,
                               std::span<const std::byte> fragment_spirv,
                               pipeline_resources& resources) -> result<void> {
    auto descriptor_layout_result = create_descriptor_layout(device);
    if (!descriptor_layout_result) {
        return std::unexpected(descriptor_layout_result.error());
    }
    resources.descriptor_layout = *descriptor_layout_result;

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.size = sizeof(draw_constants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &resources.descriptor_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    VkResult result =
        vkCreatePipelineLayout(device, &layout_info, nullptr, &resources.pipeline_layout);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreatePipelineLayout failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    auto pipeline_result = create_pipeline(device, resources.clear_pass, resources.pipeline_layout,
                                           vertex_spirv, fragment_spirv);
    if (!pipeline_result) {
        return std::unexpected(pipeline_result.error());
    }
    resources.pipeline = *pipeline_result;

    auto pool_result = create_descriptor_pool(device);
    if (!pool_result) {
        return std::unexpected(pool_result.error());
    }
    resources.descriptor_pool = *pool_result;

    auto set_result =
        allocate_descriptor_set(device, resources.descriptor_pool, resources.descriptor_layout);
    if (!set_result) {
        return std::unexpected(set_result.error());
    }
    resources.descriptor_set = *set_result;
    return {};
}

auto build_pipeline_resources(VkDevice device, VkFormat format,
                              std::span<const std::byte> vertex_spirv,
                              std::span<const std::byte> fragment_spirv,
                              pipeline_resources& resources) -> result<void> {
    if (auto passes = build_render_passes(device, format, resources); !passes) {
        destroy_pipeline_resources(device, resources);
        return std::unexpected(passes.error());
    }
    if (auto pipeline = build_descriptor_pipeline(device, vertex_spirv, fragment_spirv, resources);
        !pipeline) {
        destroy_pipeline_resources(device, resources);
        return std::unexpected(pipeline.error());
    }
    return {};
}

}

auto scene_renderer::create(const vulkanpp::device& device, target_format format,
                            std::span<const std::byte> vertex_spirv,
                            std::span<const std::byte> fragment_spirv) noexcept
    -> result<scene_renderer> {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    pipeline_resources resources{};
    if (auto built = build_pipeline_resources(native_device, to_vk_format(format), vertex_spirv,
                                              fragment_spirv, resources);
        !built) {
        return std::unexpected(built.error());
    }

    const std::array<std::byte, 4> white{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                         std::byte{0xFF}};
    auto fallback_result = vulkanpp::create_texture(device, 1, 1, white);
    if (!fallback_result) {
        destroy_pipeline_resources(native_device, resources);
        return std::unexpected(fallback_result.error());
    }
    vulkanpp::texture fallback = std::move(*fallback_result);

    auto pool_result = vulkanpp::create_command_pool(device);
    if (!pool_result) {
        fallback.destroy();
        destroy_pipeline_resources(native_device, resources);
        return std::unexpected(pool_result.error());
    }
    auto pool = raii{std::move(*pool_result)};

    auto command_result = pool->allocate();
    if (!command_result) {
        fallback.destroy();
        destroy_pipeline_resources(native_device, resources);
        return std::unexpected(command_result.error());
    }
    auto command = raii{std::move(*command_result)};

    auto fence_result = vulkanpp::create_fence(device, false);
    if (!fence_result) {
        fallback.destroy();
        destroy_pipeline_resources(native_device, resources);
        return std::unexpected(fence_result.error());
    }
    auto fence = raii{std::move(*fence_result)};

    return scene_renderer{&device,
                          resources.clear_pass,
                          resources.load_pass,
                          resources.pipeline_layout,
                          resources.pipeline,
                          resources.descriptor_layout,
                          resources.descriptor_pool,
                          resources.descriptor_set,
                          std::move(fallback),
                          pool.release(),
                          command.release(),
                          fence.release(),
                          format,
                          0,
                          0};
}

auto scene_renderer::valid() const noexcept -> bool {
    return pipeline_ != nullptr;
}

auto scene_renderer::find_target(void* image, u32 width_px, u32 height_px) noexcept
    -> result<target_entry*> {
    for (auto& target : targets_) {
        if (target.image == image && target.width_px == width_px && target.height_px == height_px) {
            return &target;
        }
        if (target.image == image) {
            destroy_target(*device_, target);
            break;
        }
    }
    return create_target(image, width_px, height_px);
}

auto scene_renderer::create_target(void* image, u32 width_px, u32 height_px) noexcept
    -> result<target_entry*> {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device_->native_handle());
    auto view_result =
        create_view(native_device, reinterpret_cast<VkImage>(image), to_vk_format(format_));
    if (!view_result) {
        return std::unexpected(view_result.error());
    }

    auto clear_result =
        create_framebuffer(native_device, reinterpret_cast<VkRenderPass>(render_pass_clear_),
                           *view_result, width_px, height_px);
    if (!clear_result) {
        vkDestroyImageView(native_device, *view_result, nullptr);
        return std::unexpected(clear_result.error());
    }

    auto load_result =
        create_framebuffer(native_device, reinterpret_cast<VkRenderPass>(render_pass_load_),
                           *view_result, width_px, height_px);
    if (!load_result) {
        vkDestroyFramebuffer(native_device, *clear_result, nullptr);
        vkDestroyImageView(native_device, *view_result, nullptr);
        return std::unexpected(load_result.error());
    }

    targets_.push_back(target_entry{
        .image = image,
        .width_px = width_px,
        .height_px = height_px,
        .view = *view_result,
        .framebuffer_clear = *clear_result,
        .framebuffer_load = *load_result,
    });
    return &targets_.back();
}

auto scene_renderer::update_descriptors(std::span<const texture_binding> textures) noexcept
    -> void {
    active_bindings_.clear();
    const u32 count =
        textures.size() < max_textures ? static_cast<u32>(textures.size()) : max_textures;
    for (u32 index = 0; index < count; ++index) {
        if (textures[index].texture != nullptr && textures[index].texture->valid()) {
            active_bindings_.push_back(&textures[index]);
        }
    }

    std::array<VkDescriptorImageInfo, max_textures> image_infos{};
    for (auto& info : image_infos) {
        info = {
            .sampler = reinterpret_cast<VkSampler>(fallback_texture_.native_sampler()),
            .imageView = reinterpret_cast<VkImageView>(fallback_texture_.native_view()),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }
    for (usize index = 0; index < active_bindings_.size(); ++index) {
        image_infos[index] = {
            .sampler =
                reinterpret_cast<VkSampler>(active_bindings_[index]->texture->native_sampler()),
            .imageView =
                reinterpret_cast<VkImageView>(active_bindings_[index]->texture->native_view()),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = reinterpret_cast<VkDescriptorSet>(descriptor_set_);
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = max_textures;
    write.pImageInfo = image_infos.data();
    vkUpdateDescriptorSets(reinterpret_cast<VkDevice>(device_->native_handle()), 1, &write, 0,
                           nullptr);
}

auto scene_renderer::set_scissor(base::recti rect) noexcept -> bool {
    const base::recti viewport_rect{0, 0, static_cast<i32>(width_px_),
                                    static_cast<i32>(height_px_)};
    const base::recti clipped = rect.intersection(viewport_rect);
    if (clipped.is_empty()) {
        return false;
    }
    const VkRect2D scissor{{clipped.x, clipped.y},
                           {static_cast<u32>(clipped.width), static_cast<u32>(clipped.height)}};
    vkCmdSetScissor(reinterpret_cast<VkCommandBuffer>(command_.native_handle()), 0, 1, &scissor);
    return true;
}

auto scene_renderer::draw_quad(const scene::node& target, base::recti clip, f32 opacity,
                               bool shadow) noexcept -> void {
    const auto& style = target.style();
    base::recti rect = target.bounds();
    if (rect.is_empty()) {
        return;
    }
    if (shadow) {
        if (style.shadow_radius <= 0.0F) {
            return;
        }
        const i32 radius = static_cast<i32>(style.shadow_radius);
        rect = {rect.x - radius, rect.y - radius + static_cast<i32>(style.shadow_offset_y),
                rect.width + radius * 2, rect.height + radius * 2};
    }

    if (!set_scissor(rect.intersection(clip))) {
        return;
    }

    draw_constants constants{};
    constants.viewport[0] = static_cast<f32>(width_px_);
    constants.viewport[1] = static_cast<f32>(height_px_);
    constants.rect[0] = static_cast<f32>(rect.x);
    constants.rect[1] = static_cast<f32>(rect.y);
    constants.rect[2] = static_cast<f32>(rect.width);
    constants.rect[3] = static_cast<f32>(rect.height);
    constants.rotation = style.rotation;
    constants.scale = style.scale;
    constants.corner_radius = style.corner_radius;
    constants.shadow_radius = style.shadow_radius;
    constants.blur_radius = style.blur_radius;
    constants.opacity = opacity;
    constants.is_shadow = shadow ? 1 : 0;

    const scene::color& source = shadow ? style.shadow : style.fill;
    constants.color[0] = source.red;
    constants.color[1] = source.green;
    constants.color[2] = source.blue;
    constants.color[3] = source.alpha;

    if (!shadow && style.texture != 0) {
        for (usize index = 0; index < active_bindings_.size(); ++index) {
            if (active_bindings_[index]->id == style.texture) {
                constants.texture_index = static_cast<i32>(index);
                break;
            }
        }
    }

    const VkCommandBuffer command = reinterpret_cast<VkCommandBuffer>(command_.native_handle());
    vkCmdPushConstants(command, reinterpret_cast<VkPipelineLayout>(pipeline_layout_),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(constants), &constants);
    vkCmdDraw(command, 6, 1, 0, 0);
    ++statistics_.draw_calls;
    ++statistics_.quads;
}

auto scene_renderer::draw_node(const scene::node& target, base::recti clip, f32 opacity,
                               const base::recti* damage_rect) noexcept -> void {
    if (!target.visible() || opacity <= 0.0F) {
        return;
    }

    const auto& style = target.style();
    const base::recti bounds = target.bounds();
    const f32 combined_opacity = opacity * target.opacity();

    base::recti extent = bounds;
    if (style.shadow_radius > 0.0F) {
        const i32 radius = static_cast<i32>(style.shadow_radius);
        extent = {bounds.x - radius, bounds.y - radius + static_cast<i32>(style.shadow_offset_y),
                  bounds.width + radius * 2, bounds.height + radius * 2};
    }

    if (damage_rect != nullptr && !extent.intersects(*damage_rect) &&
        !bounds.intersects(*damage_rect)) {
        return;
    }

    const base::recti render_clip = damage_rect != nullptr ? clip.intersection(*damage_rect) : clip;
    if (render_clip.is_empty()) {
        return;
    }

    if (style.shadow_radius > 0.0F) {
        draw_quad(target, render_clip, combined_opacity, true);
    }
    draw_quad(target, render_clip, combined_opacity, false);

    base::recti child_clip = clip;
    if (style.clip_children) {
        child_clip = clip.intersection(bounds);
        if (child_clip.is_empty()) {
            return;
        }
    }
    for (const scene::node* child : target.children()) {
        draw_node(*child, child_clip, combined_opacity, damage_rect);
    }
}

auto scene_renderer::record_clears(void* command, const base::region& damage,
                                   vulkanpp::color clear_color) noexcept -> void {
    VkClearValue clear_value{};
    clear_value.color = {{clear_color.red, clear_color.green, clear_color.blue, clear_color.alpha}};
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = 0;
    attachment.clearValue = clear_value;

    const base::recti viewport{0, 0, static_cast<i32>(width_px_), static_cast<i32>(height_px_)};
    for (const base::recti rect : damage.rects()) {
        const base::recti clipped = rect.intersection(viewport);
        if (clipped.is_empty()) {
            continue;
        }
        VkClearRect clear_rect{};
        clear_rect.rect = {{clipped.x, clipped.y},
                           {static_cast<u32>(clipped.width), static_cast<u32>(clipped.height)}};
        clear_rect.layerCount = 1;
        vkCmdClearAttachments(reinterpret_cast<VkCommandBuffer>(command), 1, &attachment, 1,
                              &clear_rect);
    }
}

auto scene_renderer::record_draws(void* command_buffer, const scene::node& root, bool full_redraw,
                                  const base::region& damage) noexcept -> void {
    const VkCommandBuffer command = reinterpret_cast<VkCommandBuffer>(command_buffer);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      reinterpret_cast<VkPipeline>(pipeline_));
    const VkDescriptorSet descriptor_set = reinterpret_cast<VkDescriptorSet>(descriptor_set_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            reinterpret_cast<VkPipelineLayout>(pipeline_layout_), 0, 1,
                            &descriptor_set, 0, nullptr);

    const VkViewport viewport{0.0F, 0.0F, static_cast<f32>(width_px_), static_cast<f32>(height_px_),
                              0.0F, 1.0F};
    vkCmdSetViewport(command, 0, 1, &viewport);

    const base::recti full_clip{0, 0, static_cast<i32>(width_px_), static_cast<i32>(height_px_)};
    if (full_redraw) {
        for (const scene::node* child : root.children()) {
            draw_node(*child, full_clip, 1.0F, nullptr);
        }
        return;
    }

    for (const base::recti rect : damage.rects()) {
        const base::recti clipped = rect.intersection(full_clip);
        if (clipped.is_empty()) {
            continue;
        }
        for (const scene::node* child : root.children()) {
            draw_node(*child, full_clip, 1.0F, &clipped);
        }
    }
}

auto scene_renderer::record_pass(void* command_buffer, const target_entry& target, bool full_redraw,
                                 const base::region& damage, const scene::node& root,
                                 vulkanpp::color clear_color) noexcept -> void {
    const VkCommandBuffer command = reinterpret_cast<VkCommandBuffer>(command_buffer);
    VkClearValue clear_value{};
    clear_value.color = {{clear_color.red, clear_color.green, clear_color.blue, clear_color.alpha}};

    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass =
        reinterpret_cast<VkRenderPass>(full_redraw ? render_pass_clear_ : render_pass_load_);
    begin_info.framebuffer = reinterpret_cast<VkFramebuffer>(full_redraw ? target.framebuffer_clear
                                                                         : target.framebuffer_load);
    begin_info.renderArea.extent = {width_px_, height_px_};
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    if (!full_redraw) {
        record_clears(command, damage, clear_color);
    }
    record_draws(command, root, full_redraw, damage);
    vkCmdEndRenderPass(command);
}

auto scene_renderer::render(void* native_image, u32 width_px, u32 height_px,
                            const scene::node& root, std::span<const texture_binding> textures,
                            const base::region& damage, vulkanpp::color clear_color) noexcept
    -> result<void> {
    if (native_image == nullptr || width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "render target arguments are invalid"));
    }

    const auto started = std::chrono::steady_clock::now();
    statistics_ = {};
    width_px_ = width_px;
    height_px_ = height_px;

    auto target_result = find_target(native_image, width_px, height_px);
    if (!target_result) {
        return std::unexpected(target_result.error());
    }
    const target_entry& target = **target_result;
    update_descriptors(textures);

    if (auto reset = fence_.reset(); !reset) {
        return std::unexpected(reset.error());
    }
    if (auto begun = command_.begin(); !begun) {
        return std::unexpected(begun.error());
    }

    const bool full_redraw = damage.empty();
    statistics_.full_redraw = full_redraw;
    record_pass(command_.native_handle(), target, full_redraw, damage, root, clear_color);

    if (auto ended = command_.end(); !ended) {
        return std::unexpected(ended.error());
    }
    if (auto submitted = vulkanpp::submit(*device_, command_, fence_); !submitted) {
        return std::unexpected(submitted.error());
    }
    if (auto waited = fence_.wait(1000000000); !waited) {
        return std::unexpected(waited.error());
    }

    const auto finished = std::chrono::steady_clock::now();
    statistics_.cpu_ms = std::chrono::duration<f64, std::milli>(finished - started).count();
    return {};
}

auto scene_renderer::statistics() const noexcept -> const frame_statistics& {
    return statistics_;
}

auto scene_renderer::destroy() noexcept -> void {
    const VkDevice native = reinterpret_cast<VkDevice>(device_->native_handle());
    for (auto& target : targets_) {
        destroy_target(*device_, target);
    }
    targets_.clear();
    fallback_texture_.destroy();
    fence_.destroy();
    command_.destroy();
    pool_.destroy();
    vkDestroyDescriptorPool(native, reinterpret_cast<VkDescriptorPool>(descriptor_pool_), nullptr);
    vkDestroyPipeline(native, reinterpret_cast<VkPipeline>(pipeline_), nullptr);
    vkDestroyPipelineLayout(native, reinterpret_cast<VkPipelineLayout>(pipeline_layout_), nullptr);
    vkDestroyDescriptorSetLayout(
        native, reinterpret_cast<VkDescriptorSetLayout>(descriptor_layout_), nullptr);
    vkDestroyRenderPass(native, reinterpret_cast<VkRenderPass>(render_pass_load_), nullptr);
    vkDestroyRenderPass(native, reinterpret_cast<VkRenderPass>(render_pass_clear_), nullptr);
    render_pass_clear_ = nullptr;
    render_pass_load_ = nullptr;
    pipeline_layout_ = nullptr;
    pipeline_ = nullptr;
    descriptor_layout_ = nullptr;
    descriptor_pool_ = nullptr;
    descriptor_set_ = nullptr;
}

}
