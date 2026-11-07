<!-- Made by tools/docs-gen.py from devices/beyond2lte/device.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# Galaxy S10+ (`beyond2lte`)

| | |
|---|---|
| Vendor | Samsung |
| SoC | `exynos9820` |
| Configuration | `make beyond2lte_defconfig` |
| Maintainer | 0xsharkboy (README.md) |
| Repository | [`devices/beyond2lte/`](https://github.com/oosexgang/hakios-xnu-kernel/tree/main/devices/beyond2lte) |

## What works

| Part | State | |
|---|---|---|
| display | partly | UEFI's framebuffer, 1440 x 3040 at 0xCA000000, seen through the loader's start; a kernel's use of it not seen; DECON at 0x19030000, the S9's driver not built |
| cpu | no | A55s 2 and 3 (cpus=2 bootcpu=2) as a first choice: not seen |
| touch | no | sec_ts, firmware y771_beyond2_phole, at 0x48 on HSI2C 0x104B0000, the S9's bus and address; the protocol is unconfirmed; driver not built |
| buttons | no | driver not built |
| storage | no | Silicium's S10 UEFI has no UFS; the controller is at 0x13D60000; needs the kernel's own driver |
| gpu | no | Mali-G76 MP12, 0x7211, at 0x18500000, known to Mesa 25.0.7; supply on the S2MPS19 not established; driver not built |
| audio | no | CS47L93 codec as on the S9, CS35L40 amplifiers: no driver |
| battery | no | MAX77705 as on the S9, 4100 mAh; driver not built |
| wifi | no | BCM4375, 14e4:4475 on PCIe: no driver |
| bluetooth | no | BCM4375: no driver |
| cellular | no |  |
| thermal | no |  |
| camera | no |  |
| torch | no |  |
| usb | no | DWC3 at 0x10C00000, the S9's address; PHY and clocks not compared; driver not built |

The state is what has been seen on this device, written down in `devices/beyond2lte/device.conf`; a part the table does not name has not been looked at.
