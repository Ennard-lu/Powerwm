#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

fail=0
roots=()
for dir in src include tests; do
    if [[ -d "$dir" ]]; then
        roots+=("$dir")
    fi
done

if ((${#roots[@]} == 0)); then
    echo "check_policy: no source roots found"
    exit 0
fi

report() {
    local title="$1"
    local matches="$2"
    if [[ -n "$matches" ]]; then
        echo "::error title=${title}::repository policy violation"
        echo "$matches"
        fail=1
    fi
}

wayland_matches=$(grep -RInE \
    '#include[[:space:]]*<(wayland-[^>]+|wayland\.h)>' \
    "${roots[@]}" --include='*.cpp' --include='*.hpp' --include='*.h' || true)
report "no libwayland" "$wayland_matches"

exception_matches=$(grep -RInE \
    '\b(throw|try|catch)\b' \
    "${roots[@]}" --include='*.cpp' --include='*.hpp' --include='*.h' || true)
report "no exceptions" "$exception_matches"

type_matches=$(grep -RInE \
    '\b(std::)?(u?int(8|16|32|64)_t|size_t|ssize_t)\b' \
    "${roots[@]}" --include='*.cpp' --include='*.hpp' --include='*.h' \
    | grep -vE '/base/types\.hpp:' || true)
report "use pwm integer aliases" "$type_matches"

c_api_pattern='(^|[^[:alnum:]_.>])(::)?(open|close|read|write|pread|pwrite|recv|recvmsg|send|sendmsg|mmap|munmap|ioctl|epoll_create1|epoll_ctl|epoll_wait|eventfd|timerfd_create|timerfd_settime|signalfd|socket|socketpair|accept4|bind|listen|connect|fcntl|dup|dup2|pipe2|memfd_create|ftruncate|posix_fallocate|drmIoctl|drmMode[A-Za-z]*|drmSet[A-Za-z]*|drmDrop[A-Za-z]*|drmGet[A-Za-z]*|gbm_[a-z_]+|vk(Create|Destroy|Allocate|Free|Map|Unmap|Cmd|Queue|Reset|Wait|Get)[A-Za-z]*)[[:space:]]*\('
c_api_matches=$(grep -RInE "$c_api_pattern" \
    "${roots[@]}" --include='*.cpp' --include='*.hpp' --include='*.h' \
    | grep -vE '/(base|platform|render/vulkanpp)/' || true)
report "wrap C APIs with create/destroy + raii" "$c_api_matches"

c_header_matches=$(grep -RInE \
    '#include[[:space:]]*<(drm[^>]*|xf86drm[^>]*|gbm[^>]*|vulkan/[^>]*|linux/[^>]*|sys/[^>]*|unistd\.h|fcntl\.h|errno\.h)>' \
    "${roots[@]}" --include='*.cpp' --include='*.hpp' --include='*.h' \
    | grep -vE '/(base|platform|render/vulkanpp)/' || true)
report "isolate C headers in wrapper layers" "$c_header_matches"

if ((fail != 0)); then
    echo
    echo "Policy check failed. See docs/coding-style.md and docs/decisions/."
    exit 1
fi

echo "Policy check passed."
