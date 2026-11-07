# Galaxy S10+ Exynos (`beyond2lte`) bring-up

Target: Samsung **SM-G975F / beyond2lte / Exynos 9820 / Mali-G76 MP12**. Snapdragon S10+ variants are excluded.

**Nothing in this port has run on an S10+ yet.** This page records what was read from one phone, so that each later change
can name the fact it relies on. Serial numbers, IMEIs, MAC addresses, raw device trees and firmware are deliberately not kept
in this repository.

## Reconnaissance

Read on 7 November 2026 over `adb` with root, read only, from a phone running a custom Android 16 ROM on the Samsung `G975FXXSGHWC2`
bootloader, with OrangeFox recovery. Values from the running kernel and its device tree, except where marked as Samsung's source
(`ivanmeler/android_kernel_samsung_beyondlte`, branch `oneui5_beyond`, read for facts).

### Boot and storage

| Item | Value | Note |
| --- | --- | --- |
| Bootloader state | `verifiedbootstate=orange`, `warranty_bit=1`, `ulcnt=1` on `/proc/cmdline` | Unlocked. `getprop` reports `green`/`0`/locked: those are masked by the ROM, do not use them |
| BOOT | 57 671 680 bytes, `sda14`, `ANDROID!` | The same size as the S9+'s |
| RECOVERY | 67 633 152 bytes, `sda15`, `ANDROID!` | Never written by this port |
| DTB / DTBO | 8 388 608 bytes each, `sda12` / `sda13`, DT table magic `d7b7ab1e` | |
| SYSTEM | 5 976 883 200 bytes, `sda25` | **Is `super_partition` on this ROM**: it holds Android. Writing iOS there erases the ROM |
| VENDOR / PRODUCT / CACHE | 1 153 433 600 / 650 117 120 / 629 145 600 bytes | Each too small for the S9+'s 2 504 355 840-byte iOS SYSTEM |
| USERDATA | 119 126 622 208 bytes, `sda31`, file-based encryption | |
| UH / UHCFG | 2 097 152 / 4 194 304 bytes, `sda10` / `sda11` | Samsung's EL2 hypervisor |
| UFS | KLUDG4U1EA-B0C1, 4 KiB blocks, LUs `sda`-`sde` | |

### SoC

| Item | S9+ (this repository) | S10+ (read) |
| --- | --- | --- |
| CPU 0-3 | Cortex-A55 | Cortex-A55, MIDR `0x411fd050` |
| CPU 4-5 | Exynos M3, MIDR `0x531f0020` | Cortex-A75, MIDR `0x412fd0a1`, MPIDR tree `cpu@0004`-`cpu@0005` |
| CPU 6-7 | Exynos M3 | Exynos M4, MIDR `0x531f0030`, `cpu@0100`-`cpu@0101` |
| RAM | 0x80000000-0xFFFFFFFF, then from 0x880000000, up to 0x8FFFFFFFF | 8 GB: 0x80000000-0xBAAFFFFF, 0xC0000000-0xDFFFFFFF, 0xE1900000-0xFFFFFFFF, 0x880000000-0x9FFFFFFFF |
| Android reserved EL2 memory | | `el2_code` 0x87000000 + 0x200000, `el2_earlymem` 0x9FD800000 + 0x2800000 |
| Interrupt controller | GIC-400 | `arm,cortex-a15-gic` at 0x10100000 (distributor at +0x1000, CPU at +0x2000) |
| Boot framebuffer | 0xCC000000, 1440 x 2960 | `s3cfb.bootloaderfb=0xca000000`, 1440 x 3040 |
| Display | DECON 0x16030000 | `decon_f` 0x19030000, `decon_t` 0x19050000; panel id `SDC_A11013` |
| GPU | Mali-G72, `0x6221`, at 0x17500000 | Mali-G76, 12 cores r0p0, `0x7211`, core mask `0x770077`, at 0x18500000 |
| UFS controller | 0x11120000 | 0x13D60000 |
| USB | DWC3 at 0x10C00000 | DWC3 at 0x10C00000 (same address; PHY and clocks not compared) |
| Touch bus | HSI2C 0x104B0000, 0x48, S6SY761 | HSI2C 0x104B0000, 0x48, `sec_ts`, firmware `tsp_sec/y771_beyond2_phole.bin` |
| PCIe | channel 0 | `13ed0000.pcie0` |

Mesa 25.0.7's `src/panfrost/lib/pan_props.c` has `MODEL(0x7211, 0, "G76", "TNOx", HAS_ANISO, 16384, {})`, so the phone's exact product
ID is known to Panfrost, which Mesa documents as non-conformant on this GPU.

### Chips on the buses

| Function | S10+ | Same as the S9+? |
| --- | --- | --- |
| Main PMICs | S2MPS19, S2MPS20 | **No** (S2MPS18). The GPU's supply path must be established for this phone before any write |
| Other PMICs | S2MPB03 (12-0056), S2DOS05 (13-0060), S2MPB02 (9-0059) | S2MPB02 yes, the rest not established |
| Charger, gauge, USB-C | MAX77705 (14-0066) | Yes, the same chip; bus and pins not compared |
| Codec | CS47L93 on SPI 2.0 | Yes, the same chip |
| Speaker amplifiers | CS35L40 at 11-0040 and 11-0041 | **No** (MAX98512) |
| Wi-Fi | BCM4375, `14e4:4475` on PCIe, firmware `bcmdhd_sta.bin_b1` | **No** (BCM4361) |
| Bluetooth | BCM4375 (`bcm4375B1_*.hcd`) | **No** |
| Wireless charger | `mfc-charger` (15-003b) | Not established |

### What the loader's restart depends on

The loader returns a run to recovery through the PMU, so these must be right before BOOT is first written. Samsung's values
come from `drivers/soc/samsung/exynos-reboot.c`, `drivers/samsung/sec_reboot.c`, `include/linux/sec_debug.h`,
`include/linux/sec_ext.h` and `drivers/watchdog/s3c2410_wdt.c`. Not yet seen working on this phone.

| Item | S9/S9+ (`platform_starlte`) | S10+ |
| --- | --- | --- |
| PMU | 0x14060000 | **0x15860000** (`samsung,exynos9820-pmu`) |
| SWRESET | +0x400, write `1` | **+0x3A00, write `2`** (bit 1, `CONFIG_SOC_EXYNOS9810` branch not taken) |
| RST_STAT | +0x404 | +0x404 |
| INFORM3, recovery | +0x80C, `0x12345674` | +0x80C, `SEC_RESET_REASON_RECOVERY` = `0x12345674`: the same |
| INFORM2 | +0x808, `0x12345678` | +0x808, but Samsung's kernel here only sets byte [23:16] to `0x4E` (`SEC_REBOOT_NORMAL`, "LPM mode prevention") |
| Watchdogs | cluster 0 0x10050000, cluster 1 0x10060000, PMU mask +0x408/+0x40C bit 23 | cluster 0 0x10050000 (`exynos9-v1-wdt`), **cluster 2** 0x10060000 (`exynos9-v2-wdt`, `RST_STAT` bit 23) |
| Watchdog unmask | clears bit 23 of +0x408 and +0x40C | **`NONCPU_INT_EN` bit 2**: cluster 0 +0x1244, cluster 2 +0x1644. The S9's registers are not used |
| PSCI | SMC, through Carbon's hypervisor | SMC. Carbon supports the Exynos 9810 only |
| Timer | 26 MHz | 26 MHz |

### Memory that must stay out of the kernel's window

From the phone's `reserved-memory` and the UEFI's map. The S9's payload home, 0xD0000000, is taken here.

| Range | What |
| --- | --- |
| 0x80000000 + 0x1000, 0x80001000 + 0x1000 | `sec_debug_magic`, `kaslr` |
| 0x87000000 + 0x200000 | `el2_code` (Samsung's hypervisor) |
| 0x96000000 + 0x2200000, 0x98200000 + 0xE00000, 0x99000000 + 0x1B800000 | `secure_camera`, `camera_contig`, `camera` |
| 0xC0000000 + 0x80000 | `seclog_mem` |
| 0xC9000000 + 0x200000 + stack and heap | the UEFI image |
| 0xCA000000 + 0x1400000 | boot framebuffer |
| 0xD0000000 + 0x1600000 | `modem_spare` |
| 0xF0000000 + 0xB900000, 0xFB900000 + 0x2000000 | `modem_if`, `cp_ram_logging` |
| 0xEB308000 + 0x1F08000, 0xFD900000 + 0x2700000 | `debug_snapshot`: `log_pstore`, `log_s2d`, `log_arraydump`, and its other logs |
| 0x880000000 + 0xAD00000, 0x9FD800000 + 0x2800000 | `sec_rdx_bootdev`, `el2_earlymem` |

`log_pstore` is 0xEB308000 + 0x8000, Samsung's layout at another address than the S9's 0xFED10000. A marker written to `/dev/pmsg0`
under Android was read back in OrangeFox from `/sys/fs/pstore/pmsg-ramoops-0` after `adb reboot recovery` (7 November 2026): OrangeFox
attaches the same range, with `record_size` and `pmsg_size` of 16 KB each, which are the halves the loader writes. That reset was
Android's own; the loader's restart has not been seen. `abox-rmem` has no fixed address here (`alloc-ranges`, 0x3C00000), which matters only once audio does.

### The stock boot image

Header v1, page 2048, kernel at 0x10008000, ramdisk at 0x11000000, tags at 0x10000100: as the S9's. The kernel has no appended device
tree, since S-Boot uses the DTB partition, so this port appends none although Silicium's `beyond1lte.toml` asks for one.

### The UEFI

Project Silicium's `beyond1ltePkg` and `S5E9820Pkg` exist. `Status.md` marks the S10 **Inactive**: display and side buttons work,
while internal storage, USB, Windows boot and Linux boot do not, and it warns that "`BIT-16` Version is Required". That is read here
as Samsung's bootloader revision, the `G` (16) in this phone's `G975FXXSGHWC2`, which would match; Silicium does not spell this out,
and the requirement was stated for the S10, not the S10+. Release v4.1 has
`Mu-starlte.img` and no S10 image, so it must be built. Packing, from `Resources/Configs/beyond1lte.toml`: FD base 0xC9000000, size
0x200000, no compression, a DTB appended, boot image header v1. No S10+ package exists. Carbon's `Hyp.bin` is in `S5E9810Pkg` only.

This port builds `beyond1ltePkg` with HakiOS's UEFI changes, using `tools/mu-silicium-fhs.nix` on NixOS, and replaces its memory map
with `devices/beyond2lte/uefi/files/`. The added regions keep UEFI's allocator off the payload, the log and `el2_earlymem`, as Scion's
S9 map does for the first two. Silicium's map declares a 60 MB DXE heap from 0xC9240000 that overlaps the framebuffer at 0xCA000000. It
is left as it is because that map is the one Silicium reports a working display with, but it is a suspect if the UEFI misbehaves.

With no payload, HakiOS's boot manager writes diagnostics to a `logdump` partition and resets. This phone has no such partition, so
nothing reaches the flash, but the reset would restart BOOT in a loop. The first test therefore carries the loader, which restarts into
recovery (`tools/build-beyond2lte-ram.sh --loader-only`).

The S9+ port reads storage through its UEFI (`ufs=2`). That is unavailable here, so any path past a RAM run needs the kernel's own UFS
driver.

## Recovering the phone

BOOT, DTB and DTBO were saved with matching SHA-256 sums to the Git-ignored `backups/beyond2lte/`. With the phone off, Volume Up +
Bixby + Power reaches OrangeFox on this phone (tried on 7 November 2026). Reported and not tried here: Download mode is Volume Down +
Bixby with USB connected.

## Runs on the phone

### 1: UEFI and loader, no kernel (7 November 2026)

`beyond2lte-loader.img`, SHA-256 `42d4f9fa...`, from `796d991`, written to BOOT from OrangeFox with a verified readback; RECOVERY unchanged.

- The screen stayed on Samsung's splash. No HakiOS logo or loader text appeared, USB dropped at the reboot and never came back, and the
  phone did not return to recovery by itself.
- Volume Down + Power forced it off, then it started into off-mode charging, because the cable was in. Unplugging the cable did not help
  recovery entry: users report these phones need USB connected to a PC for it, which matches the attempt that worked.
- Volume Down + Bixby while plugging USB in reached Download's warning screen. A short Volume Up continued to Download mode, `04e8:685d`.
- `heimdall flash --BOOT boot.partition`, under `sudo` since NixOS denies USB write access, restored BOOT from Download. Android came back
  and the hash matched. **Heimdall 2.2.2 works on this bootloader.**
- `pmsg-ramoops-0` afterwards held Android and recovery logs and nothing of the loader. They may have overwritten it, so this proves nothing.
- Android's `last_kmsg` reports `inform2: 0x4E4E0000`, consistent with the INFORM2 byte the loader writes.

How far the run got is unknown: S-Boot may not have started the image, or the shim, UEFI or loader may have stopped before drawing.

### 2: the same with marks on the screen and a DEBUG UEFI (7 November 2026)

`beyond2lte-loader.img`, SHA-256 `06f6684e...`, from `557112b`.

**The UEFI runs on an S10+.** The screen was cleared and showed, with `Firmware Version 4.0`:

```
"DXE Heap" (0xC9240000) and "Display Reserved" (0xCA000000) Overlap!
```

That is `ValidateMemoryMap()` stopping in a loop on Silicium's own map, so nothing past it ran. The heap now sits above the framebuffer, at
0xCB400000 + 0x3C00000. The shim's marks were wiped by the UEFI, as expected. USB showed `18d1:d001` named SM-G975F for 8 seconds, from
20:13:46 to 20:13:55, which is not explained yet. With this image in BOOT the recovery key combination did not reach OrangeFox, so BOOT
was restored from Download mode first.

### 3: the same with the heap moved (7 November 2026)

`beyond2lte-loader.img`, SHA-256 `5652065f...`, from `c6d96ec`, written from OrangeFox, then `adb reboot`.

**The whole path works.** The phone came back to OrangeFox by itself 60 seconds after the reboot, including OrangeFox's own start, and
`/sys/fs/pstore/pmsg-ramoops-0` held the loader's log:

```
CurrentEL=1 MIDR=411fd050 MMFR0=101122 CNTFRQ=26000000 image=a1000000
display 1440 x 3040 at ca000000
platform beyond2lte, GICv2
payload: moved from f0000000 to d2000000 (16470 bytes; its region d2000000..d5ffffff, the panic log at d5f00000)
no kernel in payload
restart: INFORM2 4e4e0000, INFORM3 12345674 (S-Boot starts recovery); the PMU's software reset (SWRESET +3a00)
```

This establishes, on this phone:

- S-Boot starts the image with no appended DTB, and the image size and text offset in its header are accepted.
- The UEFI starts, reports the framebuffer at the address S-Boot gave, and starts the loader. The loader finds `beyond2lte` by it.
- UEFI and the loader run at **EL1** on a Cortex-A55, with Samsung's hypervisor above them. The A55 implements the 16 KB translation
  granule (`MMFR0` TGran16 = 1) that XNU needs. Nothing yet says whether the A75 or M4 do, or how XNU fares under the hypervisor.
- `CNTFRQ_EL0` reads 26 MHz, so `timer_hz` is only a fallback here.
- SWRESET at +0x3A00 with value 2, and the INFORM registers, send S-Boot to recovery. The log survives it.
- Every keep-out was taken or already held by the firmware, with none refused. The loader's own image, at its preferred 0xA1000000, is
  inside the `camera` range, which nothing uses while it runs.

The hash of the whole BOOT partition differs from the image's because the old content remains past the image's end. The check is on the
image's length.

### 4: a kernel on core 2 (7 November 2026)

`beyond2lte-ram.img`, SHA-256 `f4978e3a...`, from `47191c8`, with `loader_return_after=60 cpus=2 bootcpu=2 hello_wait=90`.

The screen froze with the loader's log ending at `jumping to ...`, and the phone did not come back in 3 minutes. Every other core answered
`AFFINITY_INFO` with -1. The device tree's PSCI node is the legacy `arm,psci` with no `AFFINITY_INFO`, and its `cpu_on` ID, 0xC4000003, is
the one the loader uses and the S9's.

How far it got is unknown. The loader sets `screen_quiet` right after that line, so nothing it prints later reaches the screen: the missing
`core N: the firmware answers ...` says nothing about CPU_ON, as an earlier version of this page wrongly concluded. The log was gone by the
time OrangeFox was reached.

### 5: a kernel on core 0 (7 November 2026)

`beyond2lte-ram.img`, SHA-256 `80e735e1...`, from `5bc3d05`, with `loader_return_after=60 cpus=1 hello_wait=90`.

The same frozen screen, ending at `jumping to ...`, with `cores for the kernel ...: 0`, and no return to recovery. CPU_ON is not involved
here, so whatever stops this run is past the loader's jump. Where is unknown, since the screen is silent from there on and nothing armed
a watchdog. The two runs may or may not share a cause.

## The kernel

On NixOS, from `core/`, with no IPSW:

```sh
$(nix-build --no-out-link tools/xnu-fhs.nix)/bin/xnu-fhs -c 'bash tools/setup-theos.sh &&
  python3 tools/hakios-config.py defconfig beyond2lte beyond2lte-ram && bash xnu-deps/setup.sh && make SHELL=/bin/bash kernel'
```

Built with clang 21, no compile errors, and only the three virtio drivers. Not run yet.

What the RAM profile's kernel still has of the S9, read before running it:

| Code | What it does here |
| --- | --- |
| `osfmk/arm64/start.s`, every core | Writes four ID registers to 0xFDA10000 + 32 x Affinity1 with the MMU off, unconditionally. On this phone that is inside `log_kernel` (0xFD910000 + 2 MB), which the loader keeps out |
| `esslog` | Only with `esslog=1`. Its 0xFDA00000 header and 0xFDA10000 buffer are the S9's, so it must not be turned on here |
| `hakios_exynos_*` calls in the kernel | Stubs, since those drivers are left out |
| `ml_phys_id_from_mpidr` (patch 1504) | Generic: it works out at boot whether MPIDR is MT-numbered. It reads the boot core only, so a tree mixing MT A55s and A75s with non-MT M4s needs checking before more than the A55s run |

## Where iOS can live

The S9+ reads iOS from its SYSTEM partition. On this phone SYSTEM holds the installed Android, so an iOS SYSTEM there replaces the
ROM. No other partition large enough is free without repartitioning. Early bring-up does not need it: a run from RAM writes BOOT only,
and Android and its data stay on flash, unbootable only until the saved BOOT is written back.

## Open questions

1. Does XNU start and keep running under what Samsung's EL2 hypervisor leaves in place? Only a run can say.
2. Which cores come when asked? The boot core is an A55 (run 3); the S9's answer for the others (cores 2 and 3, not core 1) cannot be assumed.
3. Where do the Mali rail's PMIC registers live on the S2MPS19, and what voltage does S-Boot leave?
4. Is the Y771 touch controller's protocol that of `hakios.s6sy761.c`?
