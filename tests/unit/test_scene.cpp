// SPDX-License-Identifier: Apache-2.0

#include <scene/scene.hpp>

#include <cstdlib>

namespace {

namespace scene = pwm::scene;

auto test_parent_child() -> bool {
    scene::node parent{1};
    scene::node child{2};

    if (!parent.add_child(child)) {
        return false;
    }
    if (child.parent() != &parent || parent.children().size() != 1) {
        return false;
    }
    if (parent.add_child(child)) {
        return false;
    }
    if (!parent.remove_child(child) || child.parent() != nullptr) {
        return false;
    }
    return true;
}

auto test_transaction_damage() -> bool {
    scene::node target{1};
    target.set_bounds({0, 0, 10, 10});

    scene::transaction transaction;
    transaction.move(target, {50, 50, 20, 20});
    transaction.show(target, false);

    const auto damage = transaction.damage();
    if (!damage.contains({0, 0, 10, 10})) {
        return false;
    }

    const auto committed = transaction.commit();
    if (target.bounds() != pwm::base::recti{50, 50, 20, 20} || target.visible()) {
        return false;
    }
    if (committed.empty() || !transaction.damage().empty()) {
        return false;
    }
    return true;
}

auto test_node_style_and_damage() -> bool {
    scene::scene graph;
    scene::node child{2};
    if (!graph.root().add_child(child)) {
        return false;
    }

    child.set_style({.fill = {.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 1.0F},
                     .corner_radius = 8.0F,
                     .shadow_radius = 4.0F,
                     .clip_children = true});
    child.set_texture(42);
    if (child.style().corner_radius != 8.0F || child.style().texture != 42 ||
        !child.style().clip_children) {
        return false;
    }

    child.invalidate({10, 10, 20, 20});
    const auto collected = graph.collect_damage();
    if (!collected.contains({10, 10, 20, 20})) {
        return false;
    }
    if (!graph.collect_damage().empty()) {
        return false;
    }

    graph.add_damage(pwm::base::recti{30, 30, 10, 10});
    return graph.take_damage().contains({30, 30, 10, 10}) && graph.damage().empty();
}

auto test_scene_root() -> bool {
    scene::scene scene;
    scene::node child{2};
    if (!scene.root().add_child(child)) {
        return false;
    }
    auto transaction = scene.begin();
    transaction.move(child, {10, 10, 5, 5});
    return transaction.commit().contains({10, 10, 5, 5});
}

}

int main() {
    if (!test_parent_child()) {
        return EXIT_FAILURE;
    }
    if (!test_transaction_damage()) {
        return EXIT_FAILURE;
    }
    if (!test_scene_root()) {
        return EXIT_FAILURE;
    }
    if (!test_node_style_and_damage()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}