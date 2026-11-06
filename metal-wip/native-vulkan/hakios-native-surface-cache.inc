/* SPDX-License-Identifier: MIT
 * Included in the native IOKit transport, after its connection/call helpers.
 */
#include "hakios.mali_host_sync.h"
int hakios_native_surface_cache_supported(void)
{
    if(!connection){errno=ENODEV;return -1;}
    struct hakios_mali_host_sync request={HAKIOS_MALI_HOST_SYNC_VERSION,0,0,0};
    size_t output=0;
    kern_return_t result=IOConnectCallStructMethod(connection,HAKIOS_MALI_HOST_SYNC_SELECTOR,
        &request,sizeof request,NULL,&output);
    if(result==kIOReturnSuccess)return 1;
    if(result==kIOReturnUnsupported)return 0;
    errno=EIO;return -1;
}

int hakios_native_surface_cache_sync(uint64_t token,uint64_t offset,uint64_t length)
{
    uint32_t handle=0;uint64_t size=0;
    int error=hakios_native_surface_fd_import(token,&handle,&size);
    if(error)return error;
    struct hakios_mali_host_sync request={HAKIOS_MALI_HOST_SYNC_VERSION,handle,offset,length};
    if(!hakios_mali_host_sync_range(&request,size))return EINVAL;
    return call(HAKIOS_MALI_HOST_SYNC_SELECTOR,&request,sizeof request);
}
