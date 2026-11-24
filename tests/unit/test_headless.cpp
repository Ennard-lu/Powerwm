// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <platform/headless/output.hpp>

#include <cstdlib>
#include <utility>

namespace {

namespace headless = pwm::platform::headless;

auto create_output() -> pwm::raii<headless::output> {
    auto result = headless::output::create(640, 480);
    if (!result) {
        std::exit(EXIT_FAILURE);
    }
    return pwm::raii{std::move(*result)};
}

auto test_headless_commit_cycle() -> bool {
    auto output = create_output();
    if (!output->valid() || output->width_px() != 640 || output->height_px() != 480) {
        return false;
    }

    for (int frame = 0; frame < 3; ++frame) {
        if (!output->commit()) {
            return false;
        }
        if (!output->wait_for_page_flip()) {
            return false;
        }
    }

    return output->commit_count() == 3 && output->flip_count() == 3;
}

}

int main() {
    return test_headless_commit_cycle() ? EXIT_SUCCESS : EXIT_FAILURE;
}