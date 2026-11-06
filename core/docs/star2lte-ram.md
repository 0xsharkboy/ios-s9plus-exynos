# SM-G965F RAM diagnostic

This checkout starts from Scion's core at
`37015a35377179fdef9208d439ce6db247dbdc86`, with the pinned device repositories:

| Repository | Revision |
| --- | --- |
| `Scion-Project/hakios-star2lte` | `6df18008082f07c9ab8ce1d6068b02bfbd670553` |
| `Scion-Project/hakios-starlte` | `4afaefcb7246b3bbc0c9ae52568e13724026584e` |

The first image uses the Exynos configuration, a placeholder loader identity and
the existing raw-syscall `userland/hello` as PID 1 from RAM. It does not contain
the full iOS userland. The loader budget is 60 seconds; `hello_wait=90` keeps
PID 1 from deliberately exiting before that budget. The first hardware run
on this SM-G965F verified PID 1, both A55 cores and a timed return to TWRP.

## Build

Check the device configurations and the published S9 UEFI checksums:

```sh
bash tools/build-star2lte-ram.sh --check
```

With Scion's Linux/macOS toolchain and SDK installed:

```sh
bash tools/build-star2lte-ram.sh --build
```

The helper builds the kernel, placeholder loader and hello before packaging
`build/star2lte-ram/star2lte-ram.img`. It records the config, command line,
revisions and SHA-256 sums beside the image. It never invokes ADB or a flashing
tool. The borrowed S9 UEFI is still unverified on the S9+.

The configuration is `star2lte_defconfig` plus `star2lte-ram.config`. Its
arguments are exactly `cpus=2 bootcpu=2 mali=0`: UFS and persistent Data writes
are disabled, and the other peripheral drivers have no enabling arguments.
The loader supplies `rd=md0` for the hello payload. RAM root does not make the
Samsung boot chain transient: installing this image would still replace BOOT.
RECOVERY must continue to contain TWRP.

Do not substitute `prebuilt/hakios-kernel` for this kernel build. The bundled
binary predates this fork's Exynos support; it has no Exynos/Mali driver strings.

## Hardware evidence from this phone

Read on 3 November 2026 in TWRP 3.7.0_9-0; no test image has been booted.
The source is the saved live recovery FDT, not the stock Android runtime FDT.
Raw dumps, device identifiers and radio backups remain outside Git.

| Item | Observed |
| --- | --- |
| Board | Samsung Star2 rev26, `Samsung,EXYNOS9810` |
| Bootloader | G965FXXUHFVG4 |
| GIC | distributor `0x10101000`, CPU interface `0x10102000` |
| Mali MMIO | `0x17500000`, size `0x5000` |
| GPU sysfs identity | Mali-G72, 18 cores, r0p1, product `0x6221` |
| Touch | `sec,sec_ts`, address `0x48`, HSI2C at `0x104B0000` |
| UFS | HCI `0x11120000`, vendor registers `0x11121100` |
| High RAM banks | `0x880000000 + 0x80000000`, `0x900000000 + 0x80000000` |
| BOOT size | 57,671,680 bytes |
| RECOVERY size | 68,149,248 bytes |

The GIC, GPU, touch and UFS addresses agree with Scion's S9+ description.
This does not validate firmware handoff, panel operation or any native driver.

## Current checkpoint

WSL2 is running Ubuntu 26.04.1 on this PC. The Linux checkout is
`/home/scion/scion-star2lte`; the Windows checkout holds the same local branch.
The toolchain and SDK are in `/home/scion/theos`. Build as the `scion` user.
Do not build the SDK or unpacked iOS images on `/mnt/c`: the source tree can
contain names NTFS cannot represent.

The host packages from `.github/workflows/xnu-linux.yml` also need
`libncurses-dev` (tcsh's tgetent) and `python-is-python3` (XNU's LLDB macros)
on a fresh Ubuntu install. The helper explicitly uses Bash for make recipes:
the hello recipe sources `xnu-env.sh`, which uses Bash's `BASH_SOURCE` array.

Fresh kernel, placeholder loader and hello builds have completed. XNU's only
remaining make errors are its documented kext symbol-set checks; the project's
unchanged `tools/make-kernel.sh` accepts these after a successful kernel link.
An image has been packaged and passed read-only host checks for its Android v1
header, embedded inputs, Mach-O/PE architectures, payload command line,
checksums and BOOT partition limit.

The image SHA-256 `c380430bf1feb27cf6800b25b829ffc3bd097145f98b253efe85c80ca40a26b1`
has now booted on this phone. Pstore recorded `platform star2lte`, the 4 GB
high-memory window, GICv2 timer PPI 27, `Hello from user space on Darwin!`
as PID 1 and both A55 cores online (the second after about 40 seconds).
The budget expired normally and the phone returned to TWRP. Original BOOT
was restored and hash-verified; RECOVERY retained its original TWRP hash.
Full iOS, GPU rendering and Wi-Fi were not exercised by this hello probe.

## First GPU observation on this phone

A second probe changed only `mali=0` to `mali=1` and its Android image ID.
It reached `mali: step 1: done`, then returned to TWRP and restored BOOT.
The log showed G3D already on, PLL_G3D stable at 260 MHz, ACPM PMIC/DVFS
channels usable, and BUCK6 B6OUT `0x37` (643750 uV, printed as 643 mV).
GPU registers, reset and jobs were not exercised by this step.

The original recovery kernel's `asv_table` reports this sample's stock points:

| MHz | uV |
| --- | --- |
| 260 | 643750 |
| 299 | 656250 |
| 338 | 675000 |
| 455 | 712500 |
| 546 | 737500 |
| 572 | 750000 |

The inherited driver selected 950000 uV for all rates. The source now accepts
an explicit `mali_uv` target in BUCK6's 6250-uV steps, with validation before
GPU work and readback after a write. The local `star2lte-gpu-stock` fragment
selects 750000 uV (`B6OUT=0x48`), the highest stock point on this sample.
If the existing rail exceeds the requested target, the driver refuses GPU
bring-up rather than lowering the rail under a running GPU.

The voltage-aware source at `feaacdb3` has been compiled. Kernel SHA-256 is
`bd2770f6eba5c3d38ae14eea4c645506a093a841b009ba2cc09bd716f70dc55b`.
Separate hardware cycles reached steps 2 and 4 and returned normally to TWRP.
Step 2 wrote B6OUT `0x48` and read back 750 mV; PLL_G3D remained at 260 MHz.
Step 3 made no changes because the bootloader had left G3D powered.
Step 4 read GPU_ID `0x62210010`, 18 shader cores (`0x5771777`), MMU_FEATURES
`0x2830` (48-bit GPU addresses, 40-bit physical), eight address spaces and
three job slots. This verifies register access, not job execution or rendering.
Both cycles restored and hash-verified original BOOT and unchanged RECOVERY.

Subsequent separate cycles reached steps 6 and 8. The reset completed, and
all 18 shader cores, L2 and the tiler powered up. Both write-value jobs then
completed: first identity mode, then the same job through four-level GPU page
tables at GPU VA `0x100000`. Each returned job status `0x1`, MMU fault zero
and the expected word `0xc0ffee01`. This proves job-manager and page-table
execution on the physical Mali. It does not exercise shader rendering,
Panfrost, display presentation or frame rate. Both cycles returned to TWRP
and restored original BOOT with RECOVERY unchanged.

Step 9 also completed in a separate cycle. The loader's pmsg replay includes
`HakiosMali: the GPU is there for programs to draw with`, with no service-start
error. The minimal hello did not open a GPU user client. Keep this service
initialization result separate from a client-side `mali_test` or Panfrost test.
Original BOOT was restored and RECOVERY retained its original hash.

The older hello/read-only kernel does not implement `mali_uv`; passing that
argument to it would not change its hardcoded 950000-uV behavior. It remains
unsuitable for powered GPU stages on this sample.

## A static GPU user-client probe

`bash tools/build-mali-static.sh` builds `build/mali-static/mali_static`, a raw
PID 1 program that fits in BOOT beside the kernel. It uses Apple's MIG user
stubs generated from the same XNU tree's `device.defs` and `mach_host.defs`,
with a small single-thread runtime. It needs neither dyld nor the iOS volumes.
The kernel must reach `mali=9` before its `HakiosMali` client can open.

The runtime uses the scalar `mach_msg2` ABI with kernel-object call checking;
the legacy trap is rejected by this XNU/SDK pair. Matching uses the supported
`io_service_get_matching_services_bin` routine; the older string routine
returns `kIOReturnUnsupported`. QEMU with the dedicated virtio kernel verifies
host-port lookup, matching, iterator operations and bounded waiting when the
GPU service is absent. QEMU does not exercise this physical GPU or its client.

On hardware the program is intended to open the real user client, query its
GPU ID, create and map a buffer, submit three write-value jobs, wait for their
completion, verify distinct readbacks, unmap and release the buffer and close
the service. `PASS` requires all operations and all three readbacks to succeed.
This is a transport and memory test, not a shader or rendering test. It never
exits PID 1: a loader time budget must end the run. The image and hardware
result must be checked before treating a new build as client-side evidence.

The client built at `e23bb446` (33,912 bytes, SHA-256
`164c228dbe2e29a572bcaaa5282f825c4fbe143ef5865509aee297c222c4a075`)
has now passed on this S9+. Image SHA-256 is
`e2d441d9d466bf321cdbf75723ccefbcf42b8ee5d848aa0ce51db3d691cd8025`.
It opened `HakiosMali`, queried GPU product `0x6221`, created buffer handle 1
at GPU VA `0x2000000`, mapped 16 KB at CPU VA `0x10000c000`, and completed
three jobs with readbacks `0xc0ffee01`, `0xc0ffee02`, `0xc0ffee03` and status 1.
Both wait operations, unmap, GEM close and service close returned success.
The kernel independently logged three client job chains and zero unfinished.
The program reported `PASS 3 real user-client GPU jobs`; the timed return,
original BOOT restoration and unchanged TWRP hashes were verified again.
This establishes the real client path. Shader execution and rendering remain
unverified; no frame-rate claim follows from these three write-value jobs.

## First physical Panfrost compute shader

`tools/air/gpu_probe.ll` is an own AIR module that reads 17 from one buffer,
multiplies by three, adds seven and writes a different buffer initially filled
with `0xa5a5a5a5`. `tools/mali-drmshim/compute_capture.c` compiles and dispatches
it through libhakiosair/Panfrost on the build host. The opt-in
`HAKIOS_MALI_CAPTURE=DIR` shim records parameters, allocation/free order and
buffer bytes at submission; it does not execute a GPU. Softpipe returned 58;
the capture run retained the sentinel. The captured chain contains a type-4
compute job and Bifrost code, not a write-value job.

`tools/mali-drmshim/make_replay.py` validates the capture and emits input data
for `MALI_REPLAY_CAPTURE=DIR bash tools/build-mali-static.sh`. The static
client compares every captured parameter and every allocated VA before
submission. It copies six captured buffers, submits through HakiosMali,
waits, checks the output and surrounding sentinels and releases the buffers.
On failure it retains the buffers until reset, avoiding memory reuse under a
possibly stalled GPU. `replay_check.py DIR` passed on the Linux build host:
the valid capture was accepted; cyclic jobs, missing job buffers, an output
already containing the answer, invalid VAs and duplicate IDs were rejected.

The first phone attempt refused four assumed values before submission:
THREAD_FEATURES is `0x0a046000`; TEXTURE_FEATURES1/2/3 are `0x00ff0fff`,
`0xfe3fffff`, zero. Re-capturing with those real values passed all 15 parameter
checks and all eight VA checks on the next attempt. The shader changed the
output to `0x3a`, every wait succeeded, input and guard bytes were intact,
cleanup succeeded and the program reported
`PASS Panfrost compute shader output 58 verified`.

The passing image SHA-256 is
`2382b9c041e382063b735ce43386e8a27aaf3af58b1772fc91fd30e20d1c54fa`;
client SHA-256 is
`06438baeef9aecadc7b4fc25085ca84d457e929d8ec8159718f12c5a6a8832f8`.
Original BOOT was restored and TWRP remained unchanged after both cycles.
This verifies a physical compute shader compiled by Panfrost and replayed
through IOKit. It does not verify runtime Metal/GLES on iOS, rendered pixels,
SpringBoard or 60 fps. SpringBoard has only been attempted in QEMU so far.

## Read-only UFS service and preparation for full iOS

The same verified kernel and hello payload were tested with `mali=0 ufs=2
ufs_native=0 ufs_write=0 ufs_native_ro=1`. The loader read the GPT, validated
its header/table CRCs and found five raw UFS units. XNU published 26 partition
devices in read-only mode through the loader's 4 MB mailbox buffer. This
establishes enumeration; a filesystem read through XNU's mailbox has not yet
been verified. Image SHA-256 is
`9dbfdad81e46a0095f59fdf755f5e7a6aa7e64ae25f2723a8382f83d21443331`.
The bounded cycle returned to TWRP, restored original BOOT and verified the
unchanged RECOVERY checksum.

USERDATA's first 4096 bytes do not identify plain ext4 or f2fs, recovery has
no decrypted dm mapping, no microSD is present, and CACHE is too small for the
2,504,355,840-byte system image. The prepared full-iOS boot therefore uses
the previously validated ALONE root (10,289,152 bytes), the QEMU-tested loader
with the real IPSW identity, and `system_apfs=ufs_SYSTEM`. Its BOOT image is
28,334,080 bytes and enables the proven Mali client at 750000 uV. The kernel
and loader request no UFS writes; iOS's writable data remains in RAM.

At this preparation checkpoint, the BOOT was not yet hardware tested.
Temporarily replacing stock SYSTEM was a separate action from the earlier
BOOT probes and required a complete host/device-verified SYSTEM backup and
specific user approval. The initial plan restored both originals; the user
subsequently approved retaining iOS, as recorded below. Recovery and USERDATA
remain untouched. Restoring via
64 MB `adb push` chunks was checked across a chunk boundary in recovery RAM
(65 MB with all byte values); raw Windows adb stdin truncates at byte 0x1a
and must not be used for binary partition restoration.

## First physical SpringBoard with Mali rendering

After the user approved SYSTEM and BOOT replacement and retaining iOS instead
of restoring Android, the 2,504,355,840-byte system image was written to SYSTEM
and hash-verified. The first full-iOS BOOT image has SHA-256
`a6179cd15fef638616e6b9d2c84bed49323e169c7d8ed173f66a630af49206e6`.
The original 4,613,734,400-byte SYSTEM backup was verified against the phone
before writing (SHA-256
`f316f90f0a2ae7781fbe01052d257854cd975e78d1368a00d741f66dd7e88fcd`).

The phone log confirms a read-only APFS mount of `/dev/ufs_SYSTEM`, switching
to that root and mounting `/private/var` as a RAM filesystem. SpringBoard PID
9 created surfaces and backboardd PID 13 presented 1125x2436 BGRA frames.
The user reported seeing SpringBoard. Mali reported vertex/tiler and fragment
jobs while those frames were presented: 32 of each in one sample, and frame
samples with 242 and 175 GPU jobs. Per-frame GPU time was approximately
3.7–6.1 ms at 260 MHz. This is physical iOS rendering through Mali, beyond
the earlier isolated compute shader.

The available one-second frame samples were 14, 16, 10 and 10 presented
frames. The panel refreshed 60 times per second, but that is not 60 fps.
The software presentation/scaling path took approximately 18–21 ms per
frame with `fb_dpp=0 fb_zc=0`; the render thread also consumed CPU time.
The trace includes crashing poster/widget extensions, so this is not a
stability or application-compatibility result.

The timed run returned to TWRP, with iOS retained in SYSTEM and BOOT and
RECOVERY's original hash unchanged. USERDATA was not written. Touch, usable
applications, Wi-Fi, cameras and sustained 60 fps remain unverified. The
next bounded image enables the existing DPU scaler/zero-copy path and touch,
with a longer time budget. It changes BOOT only and uses the installed iOS
baseline for rollback instead of silently reinstalling stock BOOT.
