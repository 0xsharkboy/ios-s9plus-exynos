#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t mach_vm_address_t, mach_vm_size_t;
typedef int kern_return_t;
enum { kIOMapAnywhere = 1, kIOReturnNoMemory = 2 };
static unsigned connection = 17, mappings, unmaps;
static int map_error, unmap_error;
static uint64_t actual_size = 16384;
static unsigned mach_task_self(void) { return 37; }
static int IOConnectMapMemory64(unsigned port, unsigned handle, unsigned task,
    mach_vm_address_t *address, mach_vm_size_t *size, unsigned flags)
{
    assert(port==17 && handle==9 && task==37 && flags==kIOMapAnywhere); ++mappings;
    if (map_error) return map_error;
    *address=0x10000; *size=actual_size; return 0;
}
static int IOConnectUnmapMemory64(unsigned port, unsigned handle, unsigned task, mach_vm_address_t address)
{
    assert(port==17 && handle==9 && task==37 && address==0x10000); ++unmaps; return unmap_error;
}
#include "hakios-native-map.inc"
int main(void)
{
    size_t mapped_size=0;
    assert(hakios_native_drm_map_checked(9,1536,NULL)==(void *)-1 && !mappings);
    assert(hakios_native_drm_map_checked(9,0,&mapped_size)==(void *)-1 && !mappings);
    assert(hakios_native_drm_map_checked(9,1536,&mapped_size)==(void *)0x10000 && mapped_size==16384);
    assert(!hakios_native_drm_unmap_checked(9,(void *)0x10000) && unmaps==1);
    assert(hakios_native_drm_map_checked(9,16384,&mapped_size)==(void *)0x10000);
    assert(!hakios_native_drm_unmap_checked(9,(void *)0x10000) && unmaps==2);
    mapped_size=123;
    assert(hakios_native_drm_map_checked(9,16385,&mapped_size)==(void *)-1 && errno==EINVAL && mapped_size==123 && unmaps==3);
    map_error=kIOReturnNoMemory;
    assert(hakios_native_drm_map_checked(9,1536,&mapped_size)==(void *)-1 && errno==ENOMEM && mapped_size==123 && unmaps==3);
    map_error=3;
    assert(hakios_native_drm_map_checked(9,1536,&mapped_size)==(void *)-1 && errno==EIO && mapped_size==123);
    unmap_error=1;
    assert(hakios_native_drm_unmap_checked(9,(void *)0x10000)<0 && errno==EIO);
    puts("actual IOKit map helper: 1536-byte request within 16K mapping, undersized mapping rejected and undone, errors/output lifetime PASS");
    return 0;
}
