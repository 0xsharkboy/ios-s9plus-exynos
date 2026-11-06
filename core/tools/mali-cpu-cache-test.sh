#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_cache_tmp=$(mktemp -d)
trap 'rm -rf -- "$task_cache_tmp"' EXIT
gcc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    tools/mali_cpu_cache_test.c -o "$task_cache_tmp/policy-test"
"$task_cache_tmp/policy-test"
clang --target=aarch64-unknown-windows -ffreestanding -fsyntax-only -Wall -Wextra -Werror -Wno-unused-function \
    -x c drivers/gpu/hakios.mali_cpu_cache.h
