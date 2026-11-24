// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/triangle.hpp>

#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

namespace {

struct raw_handles {
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

auto destroy_raw(VkDevice device, raw_handles& handles) -> void {
    if (handles.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, handles.pipeline, nullptr);
    }
    if (handles.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, handles.pipeline_layout, nullptr);
    }
    if (handles.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, handles.framebuffer, nullptr);
    }
    if (handles.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, handles.view, nullptr);
    }
    if (handles.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, handles.render_pass, nullptr);
    }
    handles = {};
}

auto create_view(VkDevice device, VkImage image) -> result<VkImageView> {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_B8G8R8A8_UNORM;
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

auto create_render_pass(VkDevice device) -> result<VkRenderPass> {
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

auto create_pipeline(VkDevice device, VkRenderPass render_pass, u32 width_px, u32 height_px,
                     std::span<const std::byte> vertex_spirv,
                     std::span<const std::byte> fragment_spirv, raw_handles& handles)
    -> result<void> {
    if (vertex_spirv.size() % 4 != 0 || fragment_spirv.size() % 4 != 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "SPIR-V size is not aligned"));
    }

    VkShaderModuleCreateInfo vertex_info{};
    vertex_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_info.codeSize = vertex_spirv.size();
    vertex_info.pCode = reinterpret_cast<const u32*>(vertex_spirv.data());

    VkShaderModule vertex_module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(device, &vertex_info, nullptr, &vertex_module);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateShaderModule failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkShaderModuleCreateInfo fragment_info{};
    fragment_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_info.codeSize = fragment_spirv.size();
    fragment_info.pCode = reinterpret_cast<const u32*>(fragment_spirv.data());

    VkShaderModule fragment_module = VK_NULL_HANDLE;
    result = vkCreateShaderModule(device, &fragment_info, nullptr, &fragment_module);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertex_module, nullptr);
        return std::unexpected(make_error(error_code::io, "vkCreateShaderModule failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

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

    VkViewport viewport{0.0F, 0.0F, static_cast<float>(width_px), static_cast<float>(height_px),
                        0.0F, 1.0F};
    VkRect2D scissor{{0, 0}, {width_px, height_px}};
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

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
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    result = vkCreatePipelineLayout(device, &layout_info, nullptr, &handles.pipeline_layout);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertex_module, nullptr);
        vkDestroyShaderModule(device, fragment_module, nullptr);
        return std::unexpected(make_error(error_code::io, "vkCreatePipelineLayout failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.layout = handles.pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                       &handles.pipeline);
    vkDestroyShaderModule(device, vertex_module, nullptr);
    vkDestroyShaderModule(device, fragment_module, nullptr);
    if (result != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, handles.pipeline_layout, nullptr);
        handles.pipeline_layout = VK_NULL_HANDLE;
        return std::unexpected(make_error(error_code::io, "vkCreateGraphicsPipelines failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
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

}

auto render_triangle_to_image(const device& device, void* native_image, u32 width_px, u32 height_px,
                              std::span<const std::byte> vertex_spirv,
                              std::span<const std::byte> fragment_spirv) noexcept -> result<void> {
    if (native_image == nullptr || width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "triangle pass arguments are invalid"));
    }

    const VkDevice vk_device = reinterpret_cast<VkDevice>(device.native_handle());
    raw_handles handles{};

    auto view_result = create_view(vk_device, reinterpret_cast<VkImage>(native_image));
    if (!view_result) {
        return std::unexpected(view_result.error());
    }
    handles.view = *view_result;

    auto pass_result = create_render_pass(vk_device);
    if (!pass_result) {
        destroy_raw(vk_device, handles);
        return std::unexpected(pass_result.error());
    }
    handles.render_pass = *pass_result;

    auto pipeline_result = create_pipeline(vk_device, handles.render_pass, width_px, height_px,
                                           vertex_spirv, fragment_spirv, handles);
    if (!pipeline_result) {
        destroy_raw(vk_device, handles);
        return std::unexpected(pipeline_result.error());
    }

    auto framebuffer_result =
        create_framebuffer(vk_device, handles.render_pass, handles.view, width_px, height_px);
    if (!framebuffer_result) {
        destroy_raw(vk_device, handles);
        return std::unexpected(framebuffer_result.error());
    }
    handles.framebuffer = *framebuffer_result;

    auto pool_result = create_command_pool(device);
    if (!pool_result) {
        destroy_raw(vk_device, handles);
        return std::unexpected(pool_result.error());
    }
    auto pool = raii{std::move(*pool_result)};

    auto command_result = pool->allocate();
    if (!command_result) {
        destroy_raw(vk_device, handles);
        return std::unexpected(command_result.error());
    }
    auto command = raii{std::move(*command_result)};

    auto fence_result = create_fence(device, false);
    if (!fence_result) {
        destroy_raw(vk_device, handles);
        return std::unexpected(fence_result.error());
    }
    auto fence = raii{std::move(*fence_result)};

    if (auto reset = fence->reset(); !reset) {
        destroy_raw(vk_device, handles);
        return std::unexpected(reset.error());
    }
    if (auto begun = command->begin(); !begun) {
        destroy_raw(vk_device, handles);
        return std::unexpected(begun.error());
    }

    const VkCommandBuffer command_native =
        reinterpret_cast<VkCommandBuffer>(command->native_handle());
    const VkClearValue clear_value{};
    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = reinterpret_cast<VkRenderPass>(handles.render_pass);
    begin_info.framebuffer = reinterpret_cast<VkFramebuffer>(handles.framebuffer);
    begin_info.renderArea.extent = {width_px, height_px};
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command_native, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_native, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      reinterpret_cast<VkPipeline>(handles.pipeline));
    vkCmdDraw(command_native, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_native);

    if (auto ended = command->end(); !ended) {
        destroy_raw(vk_device, handles);
        return std::unexpected(ended.error());
    }
    if (auto submitted = submit(device, command.value(), fence.value()); !submitted) {
        destroy_raw(vk_device, handles);
        return std::unexpected(submitted.error());
    }
    if (auto waited = fence->wait(1000000000); !waited) {
        destroy_raw(vk_device, handles);
        return std::unexpected(waited.error());
    }

    destroy_raw(vk_device, handles);
    return {};
}

}