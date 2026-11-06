#!/bin/bash
# A real IOKit user-client probe that fits beside XNU in Samsung BOOT.
# Requires an already prepared XNU tree and the same toolchain as make kernel.
# Does not build the kernel, connect to a phone, flash or reboot anything.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
source xnu-env.sh
out="$PWD/build/mali-static"
sdk="$THEOS/sdks/iPhoneOS16.5.sdk"
mkdir -p "$out"
program=mali_static
extra=()
if [ -n "${MALI_REPLAY_CAPTURE:-}" ]; then
    python3 tools/mali-drmshim/make_replay.py "$MALI_REPLAY_CAPTURE" "$out/mali_replay_payload.h"
    program=mali_replay
    extra=(-DMALI_REPLAY=1)
fi
for spec in iokit:device/device mach_host:mach/mach_host; do
    name=${spec%%:*}
    definition=${spec#*:}
    mig -arch arm64 -DIOKIT=1 -DIOKIT_ALL_IPC=1 -I"$XNU_SRC/osfmk" -I"$sdk/usr/include" \
        -user "$out/${name}_user.c" -header "$out/${name}_user.h" \
        -server /dev/null -sheader /dev/null "$XNU_SRC/osfmk/$definition.defs"
    xnu-clang -arch arm64 -isysroot "$sdk" -Os -ffreestanding -fno-stack-protector \
        -fno-builtin -I"$out" -Wno-deprecated-declarations \
        -c "$out/${name}_user.c" -o "$out/${name}_user.o"
done
for name in mali_static mali_static_runtime; do
    xnu-clang -arch arm64 -isysroot "$sdk" -Os -ffreestanding -fno-stack-protector \
        -fno-builtin -Wall -Wextra -Werror -I"$out" "${extra[@]}" \
        -c "userland/src/$name.c" -o "$out/$name.o"
done
xnu-clang -arch arm64 -static -nostdlib -Wl,-dead_strip -Wl,-e,_start \
    -Wl,-pagezero_size,0x100000000 -o "$out/$program" \
    "$out/mali_static.o" "$out/mali_static_runtime.o" "$out/iokit_user.o" "$out/mach_host_user.o"
test -z "$(nm -u "$out/$program")"
echo "$out/$program: static client linked, no undefined symbols"
