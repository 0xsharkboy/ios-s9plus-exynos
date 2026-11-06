/* SPDX-License-Identifier: MIT */
#ifndef HAKIOS_NATIVE_SURFACE_FD_H
#define HAKIOS_NATIVE_SURFACE_FD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native XNU interop, not a Linux dma-buf. The caller retains the IOSurface
 * until its Vulkan allocation is released. The returned descriptor is real,
 * seekable, and owned by the caller (or transferred to vkAllocateMemory).
 * token pins its identity until release, including through dup(). No pixels
 * are stored in the temporary RAM file. Unknown descriptors are refused. */
int hakios_native_surface_fd_create(uint32_t surface, uint64_t size, uint64_t *token);
int hakios_native_surface_fd_release(uint64_t token);
int hakios_native_surface_fd_lookup(int fd, uint32_t *surface, uint64_t *size);
int hakios_native_surface_fd_bind(int fd, uint32_t handle);
int hakios_native_surface_fd_import(uint64_t token, uint32_t *handle, uint64_t *size);
void hakios_native_surface_fd_forget(uint32_t handle);
/* 1 supported, 0 old kernel, -1 I/O error. Sync returns an errno (0 success).
 * The imported Vulkan memory must stay live through the call. Both GPU->CPU
 * and CPU->GPU directions clean/invalidate to PoC; callers order their use. */
int hakios_native_surface_cache_supported(void);
int hakios_native_surface_cache_sync(uint64_t token, uint64_t offset, uint64_t length);
#ifdef __cplusplus
}
#endif
#endif
