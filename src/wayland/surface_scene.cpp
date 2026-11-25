// SPDX-License-Identifier: Apache-2.0

#include <wayland/surface_scene.hpp>

#include <algorithm>
#include <utility>

namespace pwm::wayland {

namespace {

constexpr u32 shm_argb8888 = 0;
constexpr u32 shm_xrgb8888 = 1;

}

surface_scene::surface_scene(const vulkanpp::device& device, u32 output_width,
                             u32 output_height) noexcept
    : device_(&device) {
    background_ = scene::node{1};
    background_.set_bounds({0, 0, static_cast<i32>(output_width), static_cast<i32>(output_height)});
    background_.set_style({.fill = {.red = 0.03F, .green = 0.04F, .blue = 0.06F, .alpha = 1.0F}});
    graph_.root().add_child(background_);
}

auto surface_scene::find_surface(u64 surface) noexcept -> surface_entry* {
    const auto iterator =
        std::ranges::find_if(surfaces_, [surface](const std::unique_ptr<surface_entry>& entry) {
            return entry->surface == surface;
        });
    return iterator == surfaces_.end() ? nullptr : iterator->get();
}

auto surface_scene::next_cascade() noexcept -> base::recti {
    const usize index = surfaces_.empty() ? 0 : surfaces_.size() - 1;
    return {32 + static_cast<i32>(index % 8) * 40, 32 + static_cast<i32>(index / 8) * 40, 0, 0};
}

auto surface_scene::ensure_surface(u64 surface) -> surface_entry& {
    if (surface_entry* existing = find_surface(surface); existing != nullptr) {
        return *existing;
    }
    auto entry = std::make_unique<surface_entry>();
    entry->surface = surface;
    entry->texture_id = next_texture_id_++;
    entry->node = scene::node{surface + 1000};
    entry->node.set_style({.fill = {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F}});
    entry->node.set_texture(entry->texture_id);
    entry->node.set_visible(false);
    graph_.root().add_child(entry->node);
    surface_entry* pointer = entry.get();
    surfaces_.push_back(std::move(entry));
    return *pointer;
}

auto surface_scene::on_commit(const surface_commit_event& event) -> result<void> {
    return on_commit(event.surface, event);
}

auto surface_scene::on_commit(u64 surface_key, const surface_commit_event& event) -> result<void> {
    surface_entry& entry = ensure_surface(surface_key);
    if (event.buffer == 0) {
        base::recti old_bounds = entry.node.bounds();
        entry.node.set_visible(false);
        graph_.add_damage(old_bounds);
        return {};
    }
    if (event.width <= 0 || event.height <= 0 || event.stride < event.width * 4) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "surface buffer dimensions are invalid"));
    }
    if (event.is_dmabuf) {
        if (event.fd < 0) {
            return std::unexpected(
                make_error(error_code::invalid_argument, "surface DMA-BUF is invalid"));
        }
        const bool recreate = entry.texture == nullptr || entry.buffer != event.buffer ||
                              entry.texture->width_px() != static_cast<u32>(event.width) ||
                              entry.texture->height_px() != static_cast<u32>(event.height);
        if (recreate) {
            auto created = vulkanpp::create_dma_buf_texture(
                *device_, {.fd = event.fd,
                           .width_px = static_cast<u32>(event.width),
                           .height_px = static_cast<u32>(event.height),
                           .stride_bytes = static_cast<u32>(event.stride),
                           .fourcc = event.format,
                           .modifier = event.modifier});
            if (!created) {
                return std::unexpected(created.error());
            }
            entry.texture = std::make_unique<vulkanpp::texture>(std::move(*created));
        }
        entry.buffer = event.buffer;
        const base::recti old_bounds = entry.node.bounds();
        base::recti bounds = entry.bounds;
        if (bounds.is_empty()) {
            bounds = next_cascade();
        }
        bounds.width = event.width;
        bounds.height = event.height;
        entry.bounds = bounds;
        auto transaction = graph_.begin();
        transaction.move(entry.node, bounds);
        transaction.show(entry.node, true);
        graph_.add_damage(transaction.commit());
        graph_.add_damage(old_bounds);
        rebuild_bindings();
        return {};
    }
    if (event.format != shm_argb8888 && event.format != shm_xrgb8888) {
        return std::unexpected(
            make_error(error_code::unsupported, "surface buffer format is unsupported"));
    }

    const bool opaque = event.format == shm_xrgb8888;
    std::vector<std::byte> converted;
    std::span<const std::byte> pixels = event.pixels;
    if (opaque) {
        converted.assign(event.pixels.begin(), event.pixels.end());
        for (usize index = 3; index < converted.size(); index += 4) {
            converted[index] = std::byte{0xFF};
        }
        pixels = converted;
    }

    const vulkanpp::texture_data data{
        .pixels = pixels,
        .stride_bytes = static_cast<u32>(event.stride),
        .format = vulkanpp::texture_format::bgra8,
    };
    if (entry.texture == nullptr || entry.texture->width_px() != static_cast<u32>(event.width) ||
        entry.texture->height_px() != static_cast<u32>(event.height)) {
        auto created = vulkanpp::create_texture(*device_, static_cast<u32>(event.width),
                                                static_cast<u32>(event.height), data);
        if (!created) {
            return std::unexpected(created.error());
        }
        entry.texture = std::make_unique<vulkanpp::texture>(std::move(*created));
    } else if (auto updated = entry.texture->update(data); !updated) {
        return std::unexpected(updated.error());
    }

    const base::recti old_bounds = entry.node.bounds();
    base::recti bounds = entry.bounds;
    if (bounds.is_empty()) {
        bounds = next_cascade();
    }
    bounds.width = event.width;
    bounds.height = event.height;
    entry.bounds = bounds;
    entry.buffer = event.buffer;
    auto transaction = graph_.begin();
    transaction.move(entry.node, bounds);
    transaction.show(entry.node, true);
    graph_.add_damage(transaction.commit());
    graph_.add_damage(old_bounds);
    rebuild_bindings();
    return {};
}

auto surface_scene::set_surface_bounds(u64 surface_key, base::recti bounds) -> result<void> {
    surface_entry* entry = find_surface(surface_key);
    if (entry == nullptr) {
        return std::unexpected(make_error(error_code::not_found, "surface is unknown"));
    }
    entry->bounds = bounds;
    auto transaction = graph_.begin();
    transaction.move(entry->node, bounds);
    graph_.add_damage(transaction.commit());
    return {};
}

auto surface_scene::surface_bounds(u64 surface_key) const noexcept -> std::optional<base::recti> {
    const auto iterator =
        std::ranges::find_if(surfaces_, [surface_key](const std::unique_ptr<surface_entry>& entry) {
            return entry->surface == surface_key;
        });
    if (iterator == surfaces_.end()) {
        return std::nullopt;
    }
    return (*iterator)->node.bounds();
}

auto surface_scene::hit_test(f64 x, f64 y) const noexcept -> std::optional<u64> {
    for (auto iterator = surfaces_.rbegin(); iterator != surfaces_.rend(); ++iterator) {
        const auto& entry = **iterator;
        if (!entry.node.visible() || entry.texture == nullptr) {
            continue;
        }
        const base::recti bounds = entry.node.bounds();
        if (x >= static_cast<f64>(bounds.x) && x < static_cast<f64>(bounds.right()) &&
            y >= static_cast<f64>(bounds.y) && y < static_cast<f64>(bounds.bottom())) {
            return entry.surface;
        }
    }
    return std::nullopt;
}

auto surface_scene::scene_graph() noexcept -> scene::scene& {
    return graph_;
}

auto surface_scene::textures() const noexcept -> std::span<const render::texture_binding> {
    return bindings_;
}

auto surface_scene::rebuild_bindings() -> void {
    bindings_.clear();
    for (const auto& entry : surfaces_) {
        if (entry->texture != nullptr && entry->texture->valid()) {
            bindings_.push_back({.id = entry->texture_id, .texture = entry->texture.get()});
        }
    }
}

}
