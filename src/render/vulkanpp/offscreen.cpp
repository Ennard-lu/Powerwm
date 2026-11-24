// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/offscreen.hpp>

#include <utility>
#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

namespace {

auto record_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout,
                          VkImageLayout new_layout, VkAccessFlags source_access,
                          VkAccessFlags destination_access, VkPipelineStageFlags source_stage,
                          VkPipelineStageFlags destination_stage) -> void {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

auto record_clear(VkCommandBuffer command_buffer, VkImage image, color clear_color) -> void {
    VkClearColorValue value{};
    value.float32[0] = clear_color.red;
    value.float32[1] = clear_color.green;
    value.float32[2] = clear_color.blue;
    value.float32[3] = clear_color.alpha;

    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vkCmdClearColorImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1,
                         &range);
}

auto record_copy(VkCommandBuffer command_buffer, VkImage image, VkBuffer buffer, u32 width_px,
                 u32 height_px) -> void {
    VkBufferImageCopy region{};
    region.imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    region.imageExtent = {width_px, height_px, 1};

    vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                           &region);
}

auto record_buffer_barrier(VkCommandBuffer command_buffer, VkBuffer buffer) -> void {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
}

auto submit_offscreen(void* queue, VkCommandBuffer command_buffer, VkFence fence) -> result<void> {
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    const VkResult result = vkQueueSubmit(reinterpret_cast<VkQueue>(queue), 1, &submit_info, fence);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkQueueSubmit failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

}

auto create_offscreen_target(const device& device, u32 width_px, u32 height_px) noexcept
    -> result<offscreen_target> {
    if (width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "offscreen size must be non-zero"));
    }

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

    return offscreen_target{device.native_handle(),
                            device.native_queue(),
                            image_handle.release(),
                            buffer_handle.release(),
                            pool_handle.release(),
                            command_handle.release(),
                            fence_handle.release(),
                            width_px,
                            height_px};
}

auto offscreen_target::valid() const noexcept -> bool {
    return image_.valid();
}

auto offscreen_target::width_px() const noexcept -> u32 {
    return width_px_;
}

auto offscreen_target::height_px() const noexcept -> u32 {
    return height_px_;
}

auto offscreen_target::render(color clear_color) noexcept -> result<void> {
    if (auto reset = fence_.reset(); !reset) {
        return std::unexpected(reset.error());
    }
    if (auto begun = command_buffer_.begin(); !begun) {
        return std::unexpected(begun.error());
    }

    const VkCommandBuffer command_handle =
        reinterpret_cast<VkCommandBuffer>(command_buffer_.native_handle());
    const VkImage image_handle = reinterpret_cast<VkImage>(image_.native_handle());
    const VkBuffer buffer_handle = reinterpret_cast<VkBuffer>(buffer_.native_handle());

    record_image_barrier(command_handle, image_handle, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    record_clear(command_handle, image_handle, clear_color);
    record_image_barrier(command_handle, image_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
    record_copy(command_handle, image_handle, buffer_handle, width_px_, height_px_);
    record_buffer_barrier(command_handle, buffer_handle);

    if (auto ended = command_buffer_.end(); !ended) {
        return std::unexpected(ended.error());
    }
    if (auto submitted = submit_offscreen(queue_, command_handle,
                                          reinterpret_cast<VkFence>(fence_.native_handle()));
        !submitted) {
        return std::unexpected(submitted.error());
    }
    return fence_.wait(1000000000);
}

auto offscreen_target::read_pixel(u32 x, u32 y) const noexcept -> result<color> {
    const auto* pixels = static_cast<const u8*>(buffer_.mapped_data());
    const usize offset = (static_cast<usize>(y) * width_px_ + x) * 4;
    return color{
        .red = static_cast<f32>(pixels[offset]) / 255.0F,
        .green = static_cast<f32>(pixels[offset + 1]) / 255.0F,
        .blue = static_cast<f32>(pixels[offset + 2]) / 255.0F,
        .alpha = static_cast<f32>(pixels[offset + 3]) / 255.0F,
    };
}

auto offscreen_target::destroy() const noexcept -> void {
    fence_.destroy();
    command_buffer_.destroy();
    pool_.destroy();
    buffer_.destroy();
    image_.destroy();
}

}