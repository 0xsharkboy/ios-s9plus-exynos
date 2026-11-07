# Galaxy S10+ (beyond2lte, SM-G975F, Exynos 9820)

**Nothing here has run on an S10+.** [`docs/beyond2lte-bringup.md`](../../docs/beyond2lte-bringup.md) has what was read from the phone and what remains open.

A HakiOS device repository: what is true of this device and of no other. The kernel, the loader, the drivers and the tools are
the common repository's; the exynos9820 family's facts are in `soc/`, carried with the device. How the three are put together:
[docs/porting/repositories.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/repositories.md).

| File | What |
|---|---|
| `device.conf` | what the device is, and how far each part of it works: its page in the documentation is made from it |
| `configs/beyond2lte_defconfig` | the common kernel, the SoC's fragment, this device's: `make beyond2lte_defconfig` |
| `configs/beyond2lte.config` | what is this device's own: the drivers built in, the arguments it needs |
| `soc/` | its SoC family's layer (exynos9820): `soc.conf`, `configs/exynos9820.config`, the SoC's drivers; carried here so that this one repository is all the device needs |
| `drivers/` | drivers of its own, if it needs any (`drivers/<subsystem>/conf`, as in the common repository) |

Every phone driver of the common repository is left out to begin with: their addresses and pins are the phone they were written
for. Put one back in `configs/beyond2lte.config` when it has been made to work here, and say so in `device.conf`.

Check with `tools/hakios-device.py check beyond2lte`.
