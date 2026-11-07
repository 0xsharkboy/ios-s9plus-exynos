#!/usr/bin/env bash
# Build a diagnostic SM-G975F boot image. This script never accesses a phone.
# The root and logs are in RAM; installing the image would still write BOOT.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE=${1:---check}
case "$MODE" in
--check|--build) ;;
*) echo "usage: bash tools/build-beyond2lte-ram.sh [--check|--build]" >&2; exit 2 ;;
esac

python3 tools/hakios-device.py check beyond2lte
python3 tools/hakios-config.py defconfig beyond2lte beyond2lte-ram
python3 tools/hakios-config.py check
ARGS=$(python3 tools/hakios-config.py args)
test "$ARGS" = "cpus=2 bootcpu=2" || {
    echo "Unexpected RAM profile arguments: $ARGS" >&2
    exit 1
}
if [ "$MODE" = --check ]; then
    echo "RAM profile checked; no compiler, image build or device operation was run."
    exit 0
fi

for tool in make clang lld-link python3; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
# Silicium publishes no S10 image: mkxnuimg.py needs this build of beyond1ltePkg, with HakiOS's UEFI changes.
FD=${UEFI_FD:-Mu-Silicium/Build/beyond1ltePkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd}
test -f "$FD" || { echo "Missing the S10 UEFI: $FD" >&2; exit 1; }

make SHELL="$BASH" kernel
# Placeholder identity is sufficient for the raw-syscall hello process.
# Building a full iOS userland later needs its matching real IPSW identity.
make SHELL="$BASH" loader-open hello
OUT=build/beyond2lte-ram
mkdir -p "$OUT"
# loader_return_after: the run ends itself, through the PMU's restart into recovery
LINE="loader_return_after=60 $ARGS hello_wait=90"
python3 xnuloader/mkpayload.py -o "$OUT/payload.bin" \
    loader.efi=xnuloader/loader.efi kernel=hakios-kernel ramdisk=userland/hello \
    --cmdline "$LINE"
UEFI_TARGET=beyond1lte UEFI_FD="$FD" python3 mkxnuimg.py -p "$OUT/payload.bin" -o "$OUT/beyond2lte-ram.img"
cp .config "$OUT/config"
printf '%s\n' "$LINE" > "$OUT/bootargs.txt"
git rev-parse HEAD > "$OUT/core-revision.txt"
(cd "$OUT" && sha256sum beyond2lte-ram.img payload.bin config bootargs.txt core-revision.txt > SHA256SUMS)
test "$(stat -c %s "$OUT/beyond2lte-ram.img")" -le 57671680 || { echo "The image does not fit BOOT" >&2; exit 1; }
echo "$OUT/beyond2lte-ram.img: nothing was written to a phone"
