# Metal → Vulkan: work in progress

**Not the installed/default release. Not a sustained-60-FPS claim.** Experimental images were booted during development and then rolled back to the recorded GLES BOOT.

The source here includes the frontend, native transport/tests, a source-only patch against pinned metal2vulkan, and the unfinished kernel range-cache API. Private shader corpora, Apple AIR/metallibs, device images, logs containing personal data, SSH seeds and Wi-Fi helpers are excluded.

## Verified experiments

- Metal discovery, compute, rendering, framebuffer fetch, IOSurface fallback and asynchronous API checks: **104 checks passed** in the recorded development build.
- RAM frontend replacement: the newly loaded build marker was observed both in the test process and in respawned `backboardd`; existing BOOT/RECOVERY were subsequently restored/verified.
- Isolated native Vulkan import of original IOSurface pages: **56 checks passed**, at 16 KiB and 12 MiB, including GPU writes and GPU reads after CPU changes.
- iOS `sys_dcache_flush` in the tested userspace only supplied a barrier. That failed the cached-surface visibility test. Explicit `DC CIVAC`, serviced by the Exynos kernel, made the same memory path correct.

## Unfinished

The per-line cache trap costs about **163 ms for 12 MiB** in that experiment. Bulk kernel-side range maintenance, runtime import lifetimes and compositor visibility integration remain unfinished. The included range API is WIP; the last isolated-kernel build attempt failed because the older base lacks the `fTask` member used by the draft guard. Latest source drafts are not represented as having passed the earlier binary's tests.

There are still severe widget/blur stalls, partial transitions and other UI defects. WebGL 3D, general app compatibility, App Store, camera/audio qualification and a persistent installation are not established. Cached IOSurface memory must not be falsely advertised as hardware coherent to bypass those problems.

Apply `metal2vulkan-wip.patch` to the exact base in `metal2vulkan-source.json`; retain its upstream dual-license files. The patch includes the previous bridge/runtime work, not an independent implementation or an upstream-endorsed release. Experimental native Mesa source changes stay outside Panfrost's own directories.

The scripts in `build-scripts/` are development references, not a turnkey build. Private workspace paths have been replaced with `/path/to/development-workspace`; adapt the paths and pinned dependencies before using them. Kernel range-cache drafts are in `host-sync/`, including the source patch for the failed older-base build described above.
