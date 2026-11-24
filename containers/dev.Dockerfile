# SPDX-License-Identifier: Apache-2.0

FROM gcc:16

ENV DEBIAN_FRONTEND=noninteractive
ENV CC=gcc
ENV CXX=g++

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        cmake \
        ninja-build \
        pkg-config \
        ccache \
        gdb \
        strace \
        libdrm-dev \
        libvulkan-dev \
        vulkan-validationlayers \
        mesa-vulkan-drivers \
        wayland-protocols \
        libinput-dev \
        libudev-dev \
        libxkbcommon-dev \
        libseat-dev \
        clang-format \
        clang-tidy \
        python3 \
        zstd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]
