# S9+ userland preparation

The target remains the Galaxy S9+ Exynos, with native GPU rendering at 60 Hz,
working apps and Wi-Fi, then cameras and UFS installation where possible.
The initial RAM probe has booted XNU and PID 1 on the phone, with two A55
cores online and a timed return to TWRP; original BOOT was restored. Full
iOS and installation remain unverified. The separately hashed first image
is documented in `star2lte-ram.md`.

## Build fixes

`DRIVERS=softpipe,panfrost AIR=1 tools/mk-mesa.sh` now works without Freedreno.
The AIR library explicitly links Mesa's `idep_xmlconfig`, which its Panfrost
screen needs. The optional `air_run` executable is built only with Freedreno;
the common `air_metal_test` diagnostic is built and packaged for Panfrost.
The Mali host check also builds only softpipe and Panfrost.

`userland/build.sh` accepts the phone's original `bcmdhd_sta.bin_b0/b2`,
`bcmdhd_clm.blob` and `nvram.txt_*` in `fw-wifi/`. It retains their Samsung
names: the kernel selects the module's NVRAM using the chip's OTP. No generic
NVRAM profile is guessed while preparing the image. The existing `SHARE=1`
rule continues to omit the original MAC and network credentials.

## Local evidence

Prepared in WSL2 Ubuntu, under `/home/scion/scion-star2lte`:

| Component | Evidence | Limit |
| --- | --- | --- |
| BCM4361 | `tools/mk-brcmfmac.sh` built `brcmfmac.o`; `tools/brcmfmac-test.py` passed | No radio scan, association, DHCP or Internet test on this phone |
| Panfrost | Cross-built `libOSMesa.8.dylib` and `libhakiosair.dylib`, with no Freedreno | Does not show that the physical GPU executes jobs |
| Mali job descriptors | `tools/mali-drmshim-check.sh` passed GLES and AIR descriptor checks and the softpipe reference | The shim does not execute GPU jobs; its readbacks are not GPU results |
| iOS files | Exact iPhone X iOS 16.5 20F66 IPSW downloaded from Apple and SHA-256 verified | Extracted files have not booted on the S9+ |
| APFS images | `CHECK=1` passed `apfsck -cw` and comparison of every file through the kernel's APFS reader | Host filesystem checks do not prove boot or app behavior |
| Wi-Fi firmware | Eleven files read offline from a checksum-verified copy of this phone's VENDOR | Actual module selection awaits the driver's OTP read |

The Wi-Fi object's three remaining stubs are debugfs helpers, listed in
`xnu-deps/build/brcmfmac/out/stubs.txt`. The original MAC is staged privately
from the existing EFS backup; no network passphrase has been supplied.
Host `ldid` was built from ProcursusTeam/ldid so that diagnostic programs carry
their requested entitlements.

Build the images with the configured toolchain and unpacked IPSW:

```sh
DRIVERS=softpipe,panfrost GL=1 REST=1 ALONE=1 RC=rc.launchd SYSTEM=1 CHECK=1 bash userland/build.sh
```

The output is `userland/root.apfs` (small support volume) and
`userland/system.apfs` (iOS system, about 2.5 GB). They have not been installed
to UFS. The small volume by itself is not the complete iOS filesystem.

The first QEMU GICv2 check reached init's 36 checks with no failures. With
`RC=rc.launchd` explicitly selected (ALONE otherwise chooses `rc.alone`),
launchd started SpringBoard, backboardd and wifid. The captured screen still
showed the Apple logo, and lsd repeatedly reported a missing CPU-monitor
entitlement. A usable interface and application interaction are not verified.
The run returned to its loader after the configured budget, with host exit 0.
The QEMU loader used the actual IPSW identity in a separate checkout,
`/home/scion/scion-qemu`; the phone probe's placeholder loader was preserved.

A second QEMU run used a dedicated kernel with virtio input and networking.
Apple dyld debug symbols now resolve the unchanged SpringBoard main-thread
stack at two times: `_LSContextInitCommon` while Control Center filters module
metadata through LaunchServices. The lsd main thread waits in libdispatch
from `-[_LSInstallProgressService performJournalRecovery]`. These snapshots
establish a LaunchServices dependency; they do not yet identify the cause.
The missing CPU-monitor entitlement message is not evidence that this warning
causes the wait.

All-thread captures then found lsd waiting for database seeding and issuing
synchronous container queries from `MIDataContainer`. The container daemon's
worker was in death-row cleanup at 106 seconds and idle at 166 seconds. At
220 seconds, lsd was in UserManagement's foreground-UID XPC query from
`GetEligiblePersonaForBundle`, while SpringBoard still waited for reseeding.
These are distinct observations, not proof of a permanent container-daemon
deadlock. Foreground-user/session handling is the next dependency to inspect.

## Hardware checks still required

The initial XNU probe and GPU register access have been verified on the phone.
Establish GPU reset, MMU and job execution, followed by GLES pixels and display
presentation. Only measured panel refresh and scrolling on this phone can
establish the requested 60 Hz behavior. The existing framebuffer's default is
60 Hz (`fb_hz`); a reported setting is not a performance measurement.

Wi-Fi still needs actual scans, association, address assignment and traffic
from iOS. Apps need actual launches and interaction; the host build is not
evidence that social apps work. Front and rear camera capture and any UFS
installation remain pending. RECOVERY must continue to contain TWRP.
