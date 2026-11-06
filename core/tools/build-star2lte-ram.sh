#!/bin/bash
# Build a diagnostic SM-G965F boot image. This script never accesses a phone.
# The root and logs are in RAM; installing the image would still write BOOT.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE=${1:---check}
case "$MODE" in
--check|--build) ;;
*) echo "usage: bash tools/build-star2lte-ram.sh [--check|--build]" >&2; exit 2 ;;
esac

python3 tools/hakios-device.py check star2lte starlte
python3 tools/hakios-config.py defconfig star2lte star2lte-ram
python3 tools/hakios-config.py check
ARGS=$(python3 tools/hakios-config.py args)
test "$ARGS" = "cpus=2 bootcpu=2 mali=0" || {
    echo "Unexpected RAM profile arguments: $ARGS" >&2
    exit 1
}
# These are the published Scion S9 UEFI pieces, borrowed for the S9+ probe.
# No substitute is selected silently if a different checkout changes them.
python3 - <<'PY'
import hashlib
from pathlib import Path
expected = {
    "SILICIUM_UEFI.fd": "5a46b859118fb325797d141ab492bb187bb23c605ddc44e449ade8d9b33f05cd",
    "BootShim.bin": "7445cf6035702aa5b6f98bf20a931a296845f76d3e8334f23737df9395849035",
}
for name, digest in expected.items():
    path = Path("devices/starlte/prebuilt") / name
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"UEFI checksum mismatch: {path}")
print("Scion S9 UEFI checksums match; S9+ hardware boot remains unverified.")
PY
if [ "$MODE" = --check ]; then
    echo "RAM profile checked; no compiler, image build or device operation was run."
    exit 0
fi

case "$(uname -s)" in
Linux|Darwin) ;;
*) echo "Build the kernel on Linux/macOS, not in Git Bash." >&2; exit 1 ;;
esac
for tool in make clang lld-link python3; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done

make SHELL=/bin/bash kernel
# Placeholder identity is sufficient for the raw-syscall hello process.
# Building a full iOS userland later needs its matching real IPSW identity.
make SHELL=/bin/bash loader-open hello
OUT=build/star2lte-ram
mkdir -p "$OUT"
LINE="loader_platform=star2lte loader_return_after=60 $ARGS hello_wait=90"
python3 xnuloader/mkpayload.py -o "$OUT/payload.bin" \
    loader.efi=xnuloader/loader.efi kernel=hakios-kernel ramdisk=userland/hello \
    --cmdline "$LINE"
UEFI_TARGET=starlte python3 mkxnuimg.py -p "$OUT/payload.bin" -o "$OUT/star2lte-ram.img"
cp .config "$OUT/config"
printf '%s\n' "$LINE" > "$OUT/bootargs.txt"
git rev-parse HEAD > "$OUT/core-revision.txt"
git -C devices/star2lte rev-parse HEAD > "$OUT/device-revision.txt"
git -C devices/starlte rev-parse HEAD > "$OUT/uefi-device-revision.txt"
python3 - <<'PY'
import hashlib
from pathlib import Path
out = Path("build/star2lte-ram")
with (out / "SHA256SUMS").open("w", encoding="ascii", newline="\n") as stream:
    names = ("star2lte-ram.img", "payload.bin", "config", "bootargs.txt",
             "core-revision.txt", "device-revision.txt", "uefi-device-revision.txt")
    for name in sorted(names):
        path = out / name
        stream.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {name}\n")
PY
echo "$OUT/star2lte-ram.img built. No phone partitions have been written."
