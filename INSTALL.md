# Installing the verified GLES BOOT

Supported target: **Samsung Galaxy S9+ SM-G965F, star2lte, Exynos 9810**. The baseline firmware used was `G965FXXUHFVG4`. No Snapdragon target is supported.

## Required starting state

1. TWRP is installed and working. Keep independent backups of your own partitions.
2. Your own iOS SYSTEM has already been prepared and installed. Its complete partition SHA-256 must match `profiles/installed-gles.json`. The package does not contain or write SYSTEM.
3. Your current BOOT is either the recorded original Samsung BOOT or the already-installed GLES BOOT. Other BOOTs are rejected.
4. For installation from stock BOOT, your own `/efs/wifi/.mac.info` must already be accessible in TWRP. Make EFS available **read-only** first. The installer never mounts or writes EFS, prints the MAC, or substitutes the maintainer's identity. Missing/invalid original MAC means no BOOT write.

This is deliberately an exact-build installer, not a general-purpose ROM installer. A failed prerequisite means no BOOT write. Do not edit the expected hashes to force a different image through.

## TWRP

Download the `installed-gles` installer ZIP from this repository's Releases and verify its published checksum. Install the ZIP in TWRP. The script:

- verifies the target, firmware, SYSTEM and starting BOOT;
- extracts the reviewed MAC-free BOOT template to recovery RAM;
- appends the matching starting partition's original firmware tail;
- requires the full template to match the published canonical BOOT hash;
- inserts only the installing phone's original MAC in the verified 18-byte plain-file data slot;
- backs up the current BOOT in recovery RAM, writes BOOT and verifies the complete readback;
- reads RECOVERY before and after and requires it to remain unchanged.

It leaves rebooting to you. The preserved 600-second return timer eventually returns the phone to recovery. DATA is not made persistent by this build.

## Local assembly without flashing

`python tools/assemble_boot.py --prefix-template boot-prefix.template --mac-info YOUR_OWN_MAC.info --stock-boot YOUR_OWN_STOCK_BOOT.img --output installed-gles-boot.img`

The tool validates the template, original firmware and original MAC input. Kernel, userspace support, firmware, layout and arguments are unchanged. Only the Wi-Fi identity differs per device. On the maintainer's phone, supplying its private MAC reconstructs the recorded full BOOT hash exactly; this was checked locally without flashing. Other phones have a different complete BOOT hash because they keep their own identity. The tool never accesses a phone. Original firmware tails, MAC input files and full device backups are not distributed.

The template is not a boot image to flash directly. Its MAC slot is deliberately invalid until personalized. Use the TWRP ZIP or the local assembler.

## Userspace inputs

The original system was prepared from the iPhone X iOS 16.5 **20F66** restore archive, using the Scion layout and local build tooling. The restore archive hash is recorded in the profile. Use your own permitted copy; this repository does not supply Apple binaries. `core/sysimage/` and `core/userland/build.sh` document the upstream extraction/layout process. A fresh recompile/repack is not automatically asserted to match the recorded SYSTEM bytes: verify it against the profile before attempting this exact-build installer.
