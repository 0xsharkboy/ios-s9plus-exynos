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

## Where iOS can live

The S9+ reads iOS from its SYSTEM partition. On this phone SYSTEM holds the installed Android, so an iOS SYSTEM there replaces the
ROM. No other partition large enough is free without repartitioning. Early bring-up does not need it: a run from RAM writes BOOT only,
and Android and its data stay on flash, unbootable only until the saved BOOT is written back.

## Open questions

1. Does XNU start and keep running under what Samsung's EL2 hypervisor leaves in place? Only a run can say.
2. Which core does S-Boot hand over on, and which cores come when asked? The S9's answer (cores 2 and 3, not core 1) cannot be assumed.
3. Does Project Silicium's `beyond1ltePkg` start an S10+? The framebuffer it declares matches; it has not been tried here.
4. Where do the Mali rail's PMIC registers live on the S2MPS19, and what voltage does S-Boot leave?
5. Is the Y771 touch controller's protocol that of `hakios.s6sy761.c`?
