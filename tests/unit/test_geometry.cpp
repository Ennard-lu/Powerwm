// SPDX-License-Identifier: Apache-2.0

#include <base/geometry.hpp>

#include <cstdlib>

namespace {

namespace base = pwm::base;

auto test_rect_intersection() -> bool {
    const base::rect a{0.0F, 0.0F, 10.0F, 10.0F};
    const base::rect b{5.0F, 5.0F, 10.0F, 10.0F};
    const base::rect intersection = a.intersection(b);
    return intersection.x == 5.0F && intersection.y == 5.0F && intersection.width == 5.0F &&
           intersection.height == 5.0F;
}

auto test_rect_union() -> bool {
    const base::recti a{0, 0, 10, 10};
    const base::recti b{5, 5, 10, 10};
    const base::recti united = a.united(b);
    return united.x == 0 && united.y == 0 && united.width == 15 && united.height == 15;
}

auto test_region_merge() -> bool {
    base::region region;
    region.add({0, 0, 10, 10});
    region.add({5, 5, 10, 10});
    region.add({100, 100, 10, 10});

    const base::recti bounds = region.bounds();
    return region.size() == 2 && bounds.x == 0 && bounds.y == 0 && bounds.width == 110 &&
           bounds.height == 110 && region.contains({0, 0, 15, 15});
}

auto test_region_clip() -> bool {
    base::region region;
    region.add({0, 0, 10, 10});
    region.add({5, 5, 10, 10});

    const base::region clipped = region.intersection({7, 7, 2, 2});
    return clipped.size() == 1 && clipped.bounds() == base::recti{7, 7, 2, 2};
}

}

int main() {
    if (!test_rect_intersection()) {
        return EXIT_FAILURE;
    }
    if (!test_rect_union()) {
        return EXIT_FAILURE;
    }
    if (!test_region_merge()) {
        return EXIT_FAILURE;
    }
    if (!test_region_clip()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}