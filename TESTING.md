# Verification of the publication

The phone was left in TWRP. Publication checks read the device; they did not flash a new image.

## Installed identity

Live BOOT and SYSTEM SHA-256 values matched `profiles/installed-gles.json`. The reconstructed full BOOT, using the public template, the maintainer's private original MAC and matching original firmware tail, also matched that live BOOT hash exactly. TWRP reported `star2lte`, firmware `G965FXXUHFVG4`, and version `3.7.0_9-0`; `unzip`, `sha256sum` and `blockdev` were available.

## Installer checks

The actual installer script was executed under Linux against isolated ordinary files standing in for partitions. The test used the real release ZIP and matching original BOOT bytes. Android properties and block-size queries were simulated; Python ZIP extraction supplied the host's missing `unzip -p` command.

The following checks passed:

- Wrong device: rejected, BOOT unchanged.
- Wrong SYSTEM: rejected, BOOT unchanged.
- Injected partial BOOT write failure: original BOOT restored and hash checked.
- Missing/invalid original MAC: rejected before BOOT writes.
- Successful installation: BOOT matched the template plus the authored fixture MAC and original tail; RECOVERY fixture unchanged.
- Already-installed BOOT: accepted without a write.
- Failed automatic restore: verified RAM backup retained for manual recovery.

Shell syntax and the separate local BOOT assembler were also checked. These tests validate installer logic; they are not a claim that this newly packaged ZIP was flashed on another phone.

To repeat the file-only installer checks on Linux, run `python3 tools/test_installer.py --stock-boot YOUR_OWN_STOCK_BOOT.img --zip star2lte-installed-gles-twrp.zip`. The script uses temporary files and never opens an actual block device.

## Privacy checks

The publication starts a new history. Source files and decompressed release entries were scanned for known private host/device/network identifiers and credential patterns. Inspection found the original device MAC in the existing private BOOT; that original image was **not uploaded**. The public template replaces only its 18-byte plain APFS data extent with an all-zero address. APFS metadata and every other byte are unchanged; restoring the private MAC locally restores the original full hash. No saved network profile or SSH private key is included. Generic Broadcom reference addresses in vendor NVRAM are firmware defaults, not an exported Wi-Fi profile.

Development logs, personal backups, SYSTEM, IPSW, SSH seeds and credential helpers stay outside the publication. Ignore rules are a safeguard for future development; reviewed payload selection and scanning provide the initial protection.

## Remaining limits

The source rebuild and clean userspace build are not yet proven byte-for-byte reproducible. The exactness guarantee applies to the supplied BOOT template, matching input hashes and checked full BOOT assembly, with only the target MAC supplied locally. The installer refuses mismatching SYSTEM rather than claiming another build is equivalent. Historical development notes under `core/docs/` describe earlier stages; the root README and profile describe the release.

Graphics performance, keyboard/emoji defects, Wi-Fi behavior across boots, 3D WebGL and the Metal/Vulkan work remain research issues. No sustained 60 FPS or daily-driver qualification is asserted.
