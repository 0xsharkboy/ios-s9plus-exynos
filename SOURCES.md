# Sources and component provenance

Original copyright and license notices remain with the source. No single license is asserted for every component.

| Component | Source / pin | Role and license |
| --- | --- | --- |
| Scion kernel fork | [Scion-Project/scion-kernel](https://github.com/Scion-Project/scion-kernel), formerly `hackios-xnu-kernel` | Core integration and Samsung drivers; mixed MIT/APSL as indicated per file |
| Installed kernel source snapshot | Local Scion revision `67297be6c313df34732d9757db55f692f6af3c3f`, included in `core/` | Kernel binary SHA-256 is pinned in the profile; local revision is not presented as an upstream commit |
| HakiOS upstream | [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel) | Original kernel/loader/device architecture |
| Apple XNU | [apple-oss-distributions/xnu](https://github.com/apple-oss-distributions/xnu), tag `xnu-8796.121.2`, `c54f35ca767986246321eb901baf8f5ff7923f6a` | APSL 2.0 and retained component notices; `core/LICENSE.APSL` |
| Galaxy S9+ device layer | [Scion-Project/hakios-star2lte](https://github.com/Scion-Project/hakios-star2lte), `6df18008082f07c9ab8ce1d6068b02bfbd670553` | Device configuration; shared Exynos/S9 drivers |
| Galaxy S9 shared layer | [Scion-Project/hakios-starlte](https://github.com/Scion-Project/hakios-starlte), `4afaefcb7246b3bbc0c9ae52568e13724026584e` | Shared SoC support, firmware provenance and UEFI inputs |
| UEFI / hypervisor | [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium), [Carbon-Hypervisor](https://github.com/Project-Silicium/Carbon-Hypervisor) | Existing Scion starlte UEFI; BSD-family/GPL notices, including vendor-component qualifications retained in upstream notices |
| Mesa/Panfrost GLES | [Mesa](https://gitlab.freedesktop.org/mesa/mesa), 25.0.7 plus the local patches in `baseline-support/` and `core/tools/mesa/` | Mali-G72 GLES path; retain Mesa's per-file licenses |
| Broadcom Wi-Fi | Linux `brcmfmac`/`brcmutil`, Scion's `wifi/lx/brcmfmac` layer and device-supplied firmware | Driver ISC/MIT as documented in `core/NOTICE`; device firmware remains vendor material |
| Metal frontend — WIP | [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal), `ce5e19e33e05e1873d38d818e23cabfe0ac708c3` | MIT frontend/bridge; its original notices are retained |
| Metal → Vulkan — WIP | [steelbrain/metal2vulkan](https://github.com/steelbrain/metal2vulkan), base `720ef1ce9ae2a2068999a8675717418b9416283e`, local development `af869a3c011e1e64cf7d95366ba13ab6b9dcdc5e` | GPL-2.0-or-later OR LGPL-3.0-or-later; source patch and pins in `metal-wip/` |
| Native Vulkan — WIP | Mesa commit `4e32ef686bb57be709fa176e62b2e72a884fe886` | Native OS/DRM adaptation is outside the upstream Panfrost directories |
| Apple userspace | iPhone X iOS 16.5, build 20F66, user-provided restore archive | Not distributed here; not covered by the open-source kernel license |

The baseline includes an experimental Metal plugin library inherited from upstream, but the **default compositor uses GLES**: its recorded boot arguments do not enable `camtl=1` or expose the tested Metal-on-Vulkan compositor. Merely finding a Metal-named file does not establish the active graphics backend.

The detailed inherited inventory is retained in `core/THIRD_PARTY_LICENSES.md`, `core/COMPLIANCE.md` and `core/NOTICE`. Component qualifications there are not silently converted into a blanket redistribution clearance.
