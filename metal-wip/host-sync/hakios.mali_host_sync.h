/* SPDX-License-Identifier: MIT
 * Explicit PoC cache maintenance for an IOSurface BO owned by this client.
 * Callers order GPU/CPU access before requesting clean-and-invalidate.
 */
#ifndef HAKIOS_MALI_HOST_SYNC_H
#define HAKIOS_MALI_HOST_SYNC_H
#include <stdint.h>

#define HAKIOS_MALI_HOST_SYNC_SELECTOR 0x7bu
#define HAKIOS_MALI_HOST_SYNC_VERSION 0x3153434du /* MCS1 */
struct hakios_mali_host_sync {
    uint32_t version,handle;
    uint64_t offset,length;
};

static inline int
hakios_mali_host_sync_query(const struct hakios_mali_host_sync *request)
{
    return request->version==HAKIOS_MALI_HOST_SYNC_VERSION && !request->handle &&
        !request->offset && !request->length;
}

static inline int
hakios_mali_host_sync_range(const struct hakios_mali_host_sync *request,uint64_t capacity)
{
    return request->version==HAKIOS_MALI_HOST_SYNC_VERSION && request->handle &&
        request->offset<=capacity && request->length<=capacity-request->offset;
}
#endif
