// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/triangle.hpp>

#include <utility>
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

auto create_image_view(VkDevice device, VkImage image) -> result<VkImageView> {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
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
    VkAttachmentDescription color_attachment{};
    color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
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

auto submit_triangle(VkQueue queue, VkCommandBuffer command_buffer, VkFence fence) -> result<void> {
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    const VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkQueueSubmit failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

}

auto create_triangle_target(const device& device, u32 width_px, u32 height_px,
                            std::span<const std::byte> vertex_spirv,
                            std::span<const std::byte> fragment_spirv) noexcept
    -> result<triangle_target> {
    if (width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "triangle target size must be non-zero"));
    }

    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());

    auto image_result = create_image(device, width_px, height_px);
    if (!image_result) {
        return std::unexpected(image_result.error());
    }
    auto image_handle = raii{std::move(*image_result)};

    auto buffer_result = create_readback_buffer(device, static_cast<u64>(width_px) * height_px * 4);
    if (!buffer_result) {
        return std::unexpected(buffer_result.error());
    }
    auto buffer_handle = raii{std::move(*buffer_result)};

    auto pool_result = create_command_pool(device);
    if (!pool_result) {
        return std::unexpected(pool_result.error());
    }
    auto pool_handle = raii{std::move(*pool_result)};

    auto command_result = pool_handle->allocate();
    if (!command_result) {
        return std::unexpected(command_result.error());
    }
    auto command_handle = raii{std::move(*command_result)};

    auto fence_result = create_fence(device, false);
    if (!fence_result) {
        return std::unexpected(fence_result.error());
    }
    auto fence_handle = raii{std::move(*fence_result)};

    raw_handles handles{};
    auto view_result =
        create_image_view(native_device, reinterpret_cast<VkImage>(image_handle->native_handle()));
    if (!view_result) {
        return std::unexpected(view_result.error());
    }
    handles.view = *view_result;

    auto pass_result = create_render_pass(native_device);
    if (!pass_result) {
        destroy_raw(native_device, handles);
        return std::unexpected(pass_result.error());
    }
    handles.render_pass = *pass_result;

    auto pipeline_result = create_pipeline(native_device, handles.render_pass, width_px, height_px,
                                           vertex_spirv, fragment_spirv, handles);
    if (!pipeline_result) {
        destroy_raw(native_device, handles);
        return std::unexpected(pipeline_result.error());
    }

    auto framebuffer_result =
        create_framebuffer(native_device, handles.render_pass, handles.view, width_px, height_px);
    if (!framebuffer_result) {
        destroy_raw(native_device, handles);
        return std::unexpected(framebuffer_result.error());
    }
    handles.framebuffer = *framebuffer_result;

    return triangle_target{&device,
                           image_handle.release(),
                           buffer_handle.release(),
                           pool_handle.release(),
                           command_handle.release(),
                           fence_handle.release(),
                           handles.view,
                           handles.render_pass,
                           handles.pipeline_layout,
                           handles.pipeline,
                           handles.framebuffer,
                           width_px,
                           height_px};
}

auto triangle_target::valid() const noexcept -> bool {
    return image_.valid();
}

auto triangle_target::width_px() const noexcept -> u32 {
    return width_px_;
}

auto triangle_target::height_px() const noexcept -> u32 {
    return height_px_;
}

auto triangle_target::render() noexcept -> result<void> {
    if (auto reset = fence_.reset(); !reset) {
        return std::unexpected(reset.error());
    }
    if (auto begun = command_buffer_.begin(); !begun) {
        return std::unexpected(begun.error());
    }

    const VkCommandBuffer command =
        reinterpret_cast<VkCommandBuffer>(command_buffer_.native_handle());
    const VkClearValue clear_value{};
    VkRenderPassBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = reinterpret_cast<VkRenderPass>(render_pass_);
    begin_info.framebuffer = reinterpret_cast<VkFramebuffer>(framebuffer_);
    begin_info.renderArea.extent = {width_px_, height_px_};
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      reinterpret_cast<VkPipeline>(pipeline_));
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);

    VkBufferImageCopy region{};
    region.imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    region.imageExtent = {width_px_, height_px_, 1};
    vkCmdCopyImageToBuffer(command, reinterpret_cast<VkImage>(image_.native_handle()),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           reinterpret_cast<VkBuffer>(readback_.native_handle()), 1, &region);

    VkBufferMemoryBarrier buffer_barrier{};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = reinterpret_cast<VkBuffer>(readback_.native_handle());
    buffer_barrier.offset = 0;
    buffer_barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &buffer_barrier, 0, nullptr);

    if (auto ended = command_buffer_.end(); !ended) {
        return std::unexpected(ended.error());
    }
    if (auto submitted =
            submit_triangle(reinterpret_cast<VkQueue>(device_->native_queue()), command,
                            reinterpret_cast<VkFence>(fence_.native_handle()));
        !submitted) {
        return std::unexpected(submitted.error());
    }
    return fence_.wait(1000000000);
}

auto triangle_target::read_pixel(u32 x, u32 y) const noexcept -> result<color> {
    const auto* pixels = static_cast<const u8*>(readback_.mapped_data());
    const usize offset = (static_cast<usize>(y) * width_px_ + x) * 4;
    return color{
        .red = static_cast<f32>(pixels[offset]) / 255.0F,
        .green = static_cast<f32>(pixels[offset + 1]) / 255.0F,
        .blue = static_cast<f32>(pixels[offset + 2]) / 255.0F,
        .alpha = static_cast<f32>(pixels[offset + 3]) / 255.0F,
    };
}

auto triangle_target::destroy() const noexcept -> void {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device_->native_handle());
    vkDestroyPipeline(native_device, reinterpret_cast<VkPipeline>(pipeline_), nullptr);
    vkDestroyPipelineLayout(native_device, reinterpret_cast<VkPipelineLayout>(pipeline_layout_),
                            nullptr);
    vkDestroyFramebuffer(native_device, reinterpret_cast<VkFramebuffer>(framebuffer_), nullptr);
    vkDestroyImageView(native_device, reinterpret_cast<VkImageView>(view_), nullptr);
    vkDestroyRenderPass(native_device, reinterpret_cast<VkRenderPass>(render_pass_), nullptr);
    fence_.destroy();
    command_buffer_.destroy();
    pool_.destroy();
    readback_.destroy();
    image_.destroy();
}

}