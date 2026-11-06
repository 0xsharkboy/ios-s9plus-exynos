/* Native PanVK's modern UAPI to the existing XNU Mali submit protocol.
 * Only the default JM context is supported by this kernel. Never silently
 * discard a non-default context or nonzero reserved padding.
 */
#ifndef HAKIOS_PANFROST_SUBMIT_WIRE_H
#define HAKIOS_PANFROST_SUBMIT_WIRE_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "drm-uapi/panfrost_drm.h"
struct hakios_panfrost_submit_v1 {
 uint64_t jc, in_syncs;
 uint32_t in_sync_count, out_sync;
 uint64_t bo_handles;
 uint32_t bo_handle_count, requirements;
};
_Static_assert(sizeof(struct hakios_panfrost_submit_v1)==40,"XNU submit wire size");
_Static_assert(sizeof(struct drm_panfrost_submit)==48,"modern submit UAPI size");
_Static_assert(offsetof(struct drm_panfrost_submit,jm_ctx_handle)==40,"modern context suffix");
#define HAKIOS_SUBMIT_OFFSET(field) _Static_assert(offsetof(struct hakios_panfrost_submit_v1,field)==offsetof(struct drm_panfrost_submit,field),"submit prefix: " #field)
HAKIOS_SUBMIT_OFFSET(jc);
HAKIOS_SUBMIT_OFFSET(in_syncs);
HAKIOS_SUBMIT_OFFSET(in_sync_count);
HAKIOS_SUBMIT_OFFSET(out_sync);
HAKIOS_SUBMIT_OFFSET(bo_handles);
HAKIOS_SUBMIT_OFFSET(bo_handle_count);
HAKIOS_SUBMIT_OFFSET(requirements);
#undef HAKIOS_SUBMIT_OFFSET
static inline int
hakios_submit_pack(const struct drm_panfrost_submit *request,struct hakios_panfrost_submit_v1 *wire)
{
 if (!request || !wire || request->pad) return EINVAL;
 if (request->jm_ctx_handle) return ENOTSUP;
 memcpy(wire,request,sizeof(*wire));
 return 0;
}
#endif
