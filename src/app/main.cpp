// SPDX-License-Identifier: Apache-2.0

#include <powerwm/build_info.hpp>

#include <cstdlib>
#include <print>
#include <string_view>

namespace {

namespace build_info = pwm::build_info;

void print_version() {
    std::println("{} {} ({}, {}{})", build_info::project_name, build_info::project_version,
                 build_info::git_commit_short, build_info::git_branch,
                 build_info::git_dirty ? ", dirty" : "");
}

void print_field(std::string_view name, std::string_view value) {
    std::println("{:>22}: {}", name, value);
}

void print_build_info() {
    print_field("project", build_info::project_name);
    print_field("version", build_info::project_version);
    print_field("git commit", build_info::git_commit);
    print_field("git commit short", build_info::git_commit_short);
    print_field("git branch", build_info::git_branch);
    print_field("git describe", build_info::git_describe);
    print_field("git dirty", build_info::git_dirty ? "true" : "false");
    print_field("build timestamp", build_info::build_timestamp_utc);
    print_field("build type", build_info::build_type);
    print_field("compiler", build_info::compiler_id);
    print_field("compiler version", build_info::compiler_version);
    print_field("compiler path", build_info::compiler_path);
    print_field("generator", build_info::generator);
    print_field("system", build_info::system_name);
    print_field("processor", build_info::system_processor);
    print_field("c++ standard", build_info::cxx_standard);
    print_field("c++ flags", build_info::cxx_flags);
    print_field("build directory", build_info::build_directory);
    print_field("features", build_info::build_features);
}

void print_help() {
    std::println("usage: {} [--help] [--version] [--build-info]", build_info::project_name);
    std::println("This build contains the application skeleton only.");
    std::println("The DRM bring-up tool is available as powerwm_drm_bringup.");
}

auto handle_argument(std::string_view argument) -> bool {
    if (argument == "--help" || argument == "-h") {
        print_help();
        return true;
    }
    if (argument == "--version" || argument == "-V") {
        print_version();
        return true;
    }
    if (argument == "--build-info") {
        print_build_info();
        return true;
    }
    return false;
}

}

int main(int argc, char** argv) {
    if (argc == 1) {
        print_version();
        print_help();
        return EXIT_SUCCESS;
    }

    if (argc == 2 && handle_argument(argv[1])) {
        return EXIT_SUCCESS;
    }

    print_help();
    return EXIT_FAILURE;
}