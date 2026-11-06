/* Included inside the local IOKit transport, where connection is owned. */
#define HAKIOS_NATIVE_POSIX_IMPLEMENTATION
#include "hakios-native-posix.h"
void *hakios_native_drm_map_checked(uint32_t handle, uint64_t requested_size, size_t *mapped_size)
{
    if (!requested_size || !mapped_size) { errno = EINVAL; return (void *)-1; }
    mach_vm_address_t address = 0;
    mach_vm_size_t mapped = 0;
    kern_return_t result = IOConnectMapMemory64(connection, handle, mach_task_self(),
        &address, &mapped, kIOMapAnywhere);
    if (result) { errno = result == kIOReturnNoMemory ? ENOMEM : EIO; return (void *)-1; }
    /* mmap accepts sub-page lengths; IOKit returns the full allocation's
     * rounded mapping. It must cover the request, not equal its byte length. */
    if (mapped < requested_size) {
        IOConnectUnmapMemory64(connection, handle, mach_task_self(), address);
        errno = EINVAL; return (void *)-1;
    }
    *mapped_size = (size_t)mapped;
    return (void *)(uintptr_t)address;
}
int hakios_native_drm_unmap_checked(uint32_t handle, void *address)
{
    kern_return_t result = IOConnectUnmapMemory64(connection, handle, mach_task_self(),
        (mach_vm_address_t)(uintptr_t)address);
    if (result) { errno = EIO; return -1; }
    return 0;
}
