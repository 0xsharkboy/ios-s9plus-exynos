#define HAKIOS_NATIVE_POSIX_IMPLEMENTATION
#include "hakios-native-posix.h"
#include "xf86drm.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <IOKit/IOKitLib.h>
static bool present;
static unsigned maps, unmaps, closes;
static int fail_unmap;
void *IOServiceMatching(const char *name) { assert(!strcmp(name, "HakiosMali")); return (void *)1; }
io_service_t IOServiceGetMatchingService(unsigned port, void *match) { (void)port; (void)match; return present; }
int IOObjectRelease(io_service_t service) { assert(service); return 0; }
int drmOpenWithType(const char *name, const char *bus, int type)
{ assert(!strcmp(name, "panfrost") && !bus && type == DRM_NODE_RENDER); return open("/dev/null", O_RDWR); }
void *hakios_native_drm_map_checked(uint32_t handle, uint64_t size, size_t *mapped_size)
{ assert(handle == 9 && (size == 8192 || size == 1536)); void *p=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(p!=MAP_FAILED); *mapped_size = 8192; ++maps; return p; }
int hakios_native_drm_unmap_checked(uint32_t handle, void *address)
{ assert(handle == 9); if (fail_unmap) { errno = EIO; return -1; } ++unmaps; return munmap(address,8192); }
void hakios_native_sync_close_fd(int fd) { (void)fd; ++closes; }
int main(void)
{
    drmDevicePtr devices[2] = {0};
    assert(drmGetDevices2(0, devices, 2) == 0);
    present = true; assert(drmGetDevices2(0, NULL, 0) == 1);
    assert(drmGetDevices2(0, devices, 2) == 1);
    assert(devices[0]->bustype == DRM_BUS_PLATFORM && devices[0]->available_nodes == (1 << DRM_NODE_RENDER));
    const char *path = devices[0]->nodes[DRM_NODE_RENDER];
    struct stat info; assert(!hakios_native_stat(path, &info));
    int fd = hakios_native_open(path, O_RDWR | O_CLOEXEC); assert(fd >= 0);
    assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);
    int dup_fd = hakios_native_fcntl(fd, F_DUPFD_CLOEXEC, 0); assert(dup_fd >= 0);
    assert(!hakios_native_close(fd));
    assert(hakios_native_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, dup_fd, 9 * 4096 + 1) == MAP_FAILED);
    assert(hakios_native_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, dup_fd, 9 * 4096) == MAP_FAILED);
    void *mapped = hakios_native_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, dup_fd, 9 * 4096);
    assert(mapped != MAP_FAILED && maps == 1);
    assert(hakios_native_munmap(mapped, 4096) < 0 && unmaps == 0);
    assert(hakios_native_munmap((char *)mapped + 4096, 4096) < 0 && unmaps == 0);
    fail_unmap = 1; assert(hakios_native_munmap(mapped, 8192) < 0 && unmaps == 0);
    fail_unmap = 0; assert(!hakios_native_munmap(mapped, 8192) && unmaps == 1);
    mapped = hakios_native_mmap(NULL, 1536, PROT_READ | PROT_WRITE, MAP_SHARED, dup_fd, 9 * 4096);
    assert(mapped != MAP_FAILED && maps == 2);
    assert(hakios_native_munmap((char *)mapped + 4096, 4096) < 0 && unmaps == 1);
    assert(!hakios_native_munmap(mapped, 1536) && unmaps == 2);
    const int protections[]={PROT_WRITE,PROT_READ,PROT_NONE};
    for(unsigned i=0;i<sizeof(protections)/sizeof(protections[0]);i++) {
        mapped=hakios_native_mmap(NULL,1536,protections[i],MAP_SHARED,dup_fd,9*4096);
        assert(mapped!=MAP_FAILED);
        if(protections[i]&PROT_WRITE) memset(mapped,0x42,1536);
        if(protections[i]==PROT_READ) assert(((unsigned char*)mapped)[0]==0);
        assert(!hakios_native_munmap(mapped,1536));
    }
    assert(maps==5 && unmaps==5);
    assert(hakios_native_mmap(NULL,8192,PROT_READ|PROT_EXEC,MAP_SHARED,dup_fd,9*4096)==MAP_FAILED);
    assert(!hakios_native_close(dup_fd));
    /* An unrelated mapping and file remain ordinary OS resources. */
    void *regular = hakios_native_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(regular != MAP_FAILED); memset(regular, 0xaa, 4096); assert(!hakios_native_munmap(regular, 4096));
    int ordinary = hakios_native_open("/dev/null", O_RDWR); assert(ordinary >= 0);
    assert(hakios_native_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, ordinary, 9 * 4096) == MAP_FAILED);
    assert(!hakios_native_close(ordinary)); assert(maps == 5);
    drmFreeDevices(devices, 1); assert(!devices[0]);
    assert(closes == 3);
    puts("native POSIX adapter: device enumeration, dup/close, mapping bounds, WRITE/READ/NONE protections, EXEC rejected, failed unmap retention, ordinary OS fallback PASS");
    return 0;
}
