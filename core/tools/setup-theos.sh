#!/usr/bin/env bash
# What xnu-deps/setup.sh needs of theos, without theos's installer, which wants a package manager and sudo: its Linux toolchain and
# the iPhoneOS 16.5 SDK, each at a fixed revision. What is there already is left alone.
set -euo pipefail
cd "$(dirname "$0")/.."
THEOS=${THEOS:-$PWD/xnu-deps/theos}

TOOLCHAIN_URL=https://github.com/L1ghtmann/llvm-project/releases/download/test-210562a/iOSToolchain-x86_64.tar.xz
TOOLCHAIN_SHA256=a72a7a577e2fbe2838b6b5e9c72034fa7d114af96f0e1d4b016f18730ce4056e
SDKS_COMMIT=0222fd5413cf4b9af096f37b4621afa2688572f7

mkdir -p "$THEOS/toolchain" "$THEOS/sdks"
if [ ! -x "$THEOS/toolchain/linux/iphone/bin/ld" ]; then
    echo "setup-theos: fetching the toolchain (86 MB)"
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    curl -L --fail --retry 3 -s -o "$tmp/toolchain.tar.xz" "$TOOLCHAIN_URL"
    echo "$TOOLCHAIN_SHA256  $tmp/toolchain.tar.xz" | sha256sum -c --quiet
    tar -xJf "$tmp/toolchain.tar.xz" -C "$THEOS/toolchain"
fi
if [ ! -d "$THEOS/sdks/iPhoneOS16.5.sdk/usr/include" ]; then
    echo "setup-theos: fetching the iPhoneOS 16.5 SDK (about 220 MB)"
    rm -rf "$THEOS/sdks-git"
    git init -q "$THEOS/sdks-git"
    git -C "$THEOS/sdks-git" remote add origin https://github.com/theos/sdks.git
    git -C "$THEOS/sdks-git" sparse-checkout set iPhoneOS16.5.sdk
    git -C "$THEOS/sdks-git" fetch -q --depth 1 --filter=blob:none origin "$SDKS_COMMIT"
    git -C "$THEOS/sdks-git" -c advice.detachedHead=false checkout -q FETCH_HEAD
    ln -sfn ../sdks-git/iPhoneOS16.5.sdk "$THEOS/sdks/iPhoneOS16.5.sdk"
fi
# In NixOS's FHS environment libtinfo.so.6 is a link to libncursesw, so ldconfig does not index it under this name. Linked into the
# toolchain's lib/, where libtapi, which has no search path of its own and is what asks for it, is told to look.
if [ -e /usr/lib/libtinfo.so.6 ]; then
    ln -sfn /usr/lib/libtinfo.so.6 "$THEOS/toolchain/linux/iphone/lib/libtinfo.so.6"
    patchelf --set-rpath '$ORIGIN' "$(readlink -f "$THEOS/toolchain/linux/iphone/lib/libtapi.so")"
fi
"$THEOS/toolchain/linux/iphone/bin/ld" -v 2>&1 | head -1
ls -d "$THEOS/sdks/iPhoneOS16.5.sdk/usr/include"
echo "setup-theos: done, THEOS=$THEOS"
