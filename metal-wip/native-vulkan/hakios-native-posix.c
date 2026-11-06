/* Local libdrm/POSIX integration for the XNU HakiosMali user client. */
#define HAKIOS_NATIVE_POSIX_IMPLEMENTATION
#include "hakios-native-posix.h"
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "xf86drm.h"
#include "hakios-native-sync.h"

#define RENDER_NODE "/dev/hakios-mali"
/* A descriptor returned for this virtual node still owns a real OS descriptor.
 * Keep its classification local rather than misclassifying every /dev/null fd. */
struct render_fd { int fd; struct render_fd *next; };
struct gpu_map { void *address; size_t length, mapped_length; uint32_t handle; struct gpu_map *next; };
static struct render_fd *render_fds;
static struct gpu_map *gpu_maps;
static pthread_mutex_t native_lock = PTHREAD_MUTEX_INITIALIZER;

int hakios_native_sched_setscheduler(pid_t pid, int policy, const struct sched_param *parameters)
{
    if (pid != 0) { errno = ENOTSUP; return -1; }
    int result = pthread_setschedparam(pthread_self(), policy, parameters);
    if (result) { errno = result; return -1; }
    return 0;
}

static bool mali_present(void)
{
    io_service_t service = IOServiceGetMatchingService(0, IOServiceMatching("HakiosMali"));
    if (!service) return false;
    IOObjectRelease(service);
    return true;
}

static bool render_fd_locked(int fd)
{
    for (struct render_fd *p = render_fds; p; p = p->next)
        if (p->fd == fd) return true;
    return false;
}

int hakios_native_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    }
    if (!path || strcmp(path, RENDER_NODE)) return open(path, flags, mode);
    if ((flags & O_ACCMODE) != O_RDWR || (flags & (O_CREAT | O_TRUNC | O_EXCL))) {
        errno = EINVAL; return -1;
    }
    struct render_fd *record = malloc(sizeof(*record));
    if (!record) { errno = ENOMEM; return -1; }
    pthread_mutex_lock(&native_lock);
    int fd = drmOpenWithType("panfrost", NULL, DRM_NODE_RENDER);
    if (fd >= 0 && (flags & O_CLOEXEC) && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        int saved = errno; close(fd); fd = -1; errno = saved;
    }
    if (fd >= 0) {
        record->fd = fd; record->next = render_fds; render_fds = record;
    } else free(record);
    pthread_mutex_unlock(&native_lock);
    return fd;
}

int hakios_native_close(int fd)
{
    hakios_native_sync_close_fd(fd);
    pthread_mutex_lock(&native_lock);
    struct render_fd **p = &render_fds;
    while (*p && (*p)->fd != fd) p = &(*p)->next;
    if (*p) { struct render_fd *old = *p; *p = old->next; free(old); }
    int result = close(fd);
    pthread_mutex_unlock(&native_lock);
    return result;
}

int hakios_native_fcntl(int fd, int command, ...)
{
    /* The commands used by Mesa's POSIX helpers have integer or no arguments.
     * Other commands are rejected instead of guessing the variadic ABI. */
    switch (command) {
    case F_GETFD: case F_GETFL: case F_GETOWN:
        return fcntl(fd, command);
    case F_SETFD: case F_SETFL: case F_SETOWN:
    case F_DUPFD: case F_DUPFD_CLOEXEC: {
        va_list ap; va_start(ap, command); int value = va_arg(ap, int); va_end(ap);
        if (command != F_DUPFD && command != F_DUPFD_CLOEXEC) return fcntl(fd, command, value);
        pthread_mutex_lock(&native_lock);
        struct render_fd *record = NULL;
        if (render_fd_locked(fd)) {
            record = malloc(sizeof(*record));
            if (!record) { pthread_mutex_unlock(&native_lock); errno = ENOMEM; return -1; }
        }
        int duplicate = fcntl(fd, command, value);
        if (duplicate >= 0 && record) {
            record->fd = duplicate; record->next = render_fds; render_fds = record;
        } else free(record);
        pthread_mutex_unlock(&native_lock);
        return duplicate;
    }
    default: errno = ENOTSUP; return -1;
    }
}

int hakios_native_stat(const char *path, struct stat *info)
{
    if (!path || strcmp(path, RENDER_NODE)) return stat(path, info);
    if (!mali_present()) { errno = ENOENT; return -1; }
    /* The node is an IOKit service, not Linux devfs. Its descriptor backing
     * object supplies a stable local identity; actual GPU properties are
     * obtained from the service by the driver's GET_PARAM requests. */
    return stat("/dev/null", info);
}

void *hakios_native_mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset)
{
    pthread_mutex_lock(&native_lock);
    if (!render_fd_locked(fd)) {
        pthread_mutex_unlock(&native_lock);
        return mmap(address, length, prot, flags, fd, offset);
    }
    if (!length || offset < 0 || ((uint64_t)offset & 4095) ||
        ((uint64_t)offset >> 12) > UINT32_MAX || !((uint64_t)offset >> 12) ||
        (prot & ~(PROT_READ | PROT_WRITE)) || !(flags & MAP_SHARED) ||
        flags & ~(MAP_SHARED | MAP_FIXED) || flags & MAP_FIXED) {
        pthread_mutex_unlock(&native_lock); errno = EINVAL; return MAP_FAILED;
    }
    struct gpu_map *record = malloc(sizeof(*record));
    if (!record) { pthread_mutex_unlock(&native_lock); errno = ENOMEM; return MAP_FAILED; }
    uint32_t handle = (uint32_t)((uint64_t)offset >> 12);
    size_t mapped_length = 0;
    void *mapped = hakios_native_drm_map_checked(handle, length, &mapped_length);
    if (mapped == MAP_FAILED) { free(record); pthread_mutex_unlock(&native_lock); return MAP_FAILED; }
    /* PanVK maps upload pools with PROT_WRITE alone. Honor all non-executable
     * access modes instead of accepting only the read/write combination. */
    if (prot != (PROT_READ | PROT_WRITE) && mprotect(mapped, mapped_length, prot)) {
        int saved = errno;
        hakios_native_drm_unmap_checked(handle, mapped);
        free(record); pthread_mutex_unlock(&native_lock); errno = saved;
        return MAP_FAILED;
    }
    record->address = mapped; record->length = length; record->handle = handle;
    record->mapped_length = mapped_length;
    record->next = gpu_maps; gpu_maps = record;
    pthread_mutex_unlock(&native_lock);
    return mapped;
}

int hakios_native_munmap(void *address, size_t length)
{
    pthread_mutex_lock(&native_lock);
    struct gpu_map **p = &gpu_maps;
    while (*p && (*p)->address != address) p = &(*p)->next;
    if (!*p) {
        uintptr_t begin = (uintptr_t)address;
        if (length > UINTPTR_MAX - begin) { pthread_mutex_unlock(&native_lock); errno = EINVAL; return -1; }
        for (struct gpu_map *map = gpu_maps; map; map = map->next) {
            uintptr_t mapped_begin = (uintptr_t)map->address;
            if (begin < mapped_begin + map->mapped_length && begin + length > mapped_begin) {
                pthread_mutex_unlock(&native_lock); errno = EINVAL; return -1;
            }
        }
        pthread_mutex_unlock(&native_lock);
        return munmap(address, length);
    }
    if ((*p)->length != length) { pthread_mutex_unlock(&native_lock); errno = EINVAL; return -1; }
    struct gpu_map *old = *p;
    if (hakios_native_drm_unmap_checked(old->handle, address)) {
        pthread_mutex_unlock(&native_lock); return -1;
    }
    *p = old->next; free(old);
    pthread_mutex_unlock(&native_lock);
    return 0;
}

static drmDevicePtr allocate_device(void)
{
    drmDevicePtr device = calloc(1, sizeof(*device));
    if (!device) return NULL;
    device->bustype = DRM_BUS_PLATFORM;
    device->nodes = calloc(DRM_NODE_MAX, sizeof(*device->nodes));
    device->businfo.platform = calloc(1, sizeof(*device->businfo.platform));
    device->deviceinfo.platform = calloc(1, sizeof(*device->deviceinfo.platform));
    if (!device->nodes || !device->businfo.platform || !device->deviceinfo.platform) goto fail;
    device->nodes[DRM_NODE_RENDER] = strdup(RENDER_NODE);
    device->deviceinfo.platform->compatible = calloc(2, sizeof(char *));
    if (!device->nodes[DRM_NODE_RENDER] || !device->deviceinfo.platform->compatible) goto fail;
    device->deviceinfo.platform->compatible[0] = strdup("arm,mali-bifrost");
    if (!device->deviceinfo.platform->compatible[0]) goto fail;
    strcpy(device->businfo.platform->fullname, "HakiosMali");
    device->available_nodes = 1 << DRM_NODE_RENDER;
    return device;
fail:
    drmFreeDevice(&device);
    return NULL;
}

int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices)
{
    if (flags & ~DRM_DEVICE_GET_PCI_REVISION || max_devices < 0) return -EINVAL;
    if (!mali_present()) return 0;
    if (!devices || max_devices == 0) return 1;
    drmDevicePtr device = allocate_device();
    if (!device) return -ENOMEM;
    devices[0] = device;
    return 1;
}

void drmFreeDevice(drmDevicePtr *pointer)
{
    if (!pointer || !*pointer) return;
    drmDevicePtr device = *pointer;
    if (device->nodes) {
        for (int i = 0; i < DRM_NODE_MAX; ++i) free(device->nodes[i]);
        free(device->nodes);
    }
    free(device->businfo.platform);
    if (device->deviceinfo.platform) {
        char **compatible = device->deviceinfo.platform->compatible;
        if (compatible) { for (char **p = compatible; *p; ++p) free(*p); free(compatible); }
        free(device->deviceinfo.platform);
    }
    free(device); *pointer = NULL;
}

void drmFreeDevices(drmDevicePtr devices[], int count)
{
    if (!devices) return;
    for (int i = 0; i < count; ++i) drmFreeDevice(&devices[i]);
}

int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
    if (!device || flags & ~DRM_DEVICE_GET_PCI_REVISION) return -EINVAL;
    *device = NULL;
    pthread_mutex_lock(&native_lock);
    bool is_render = render_fd_locked(fd);
    pthread_mutex_unlock(&native_lock);
    if (!is_render) return -ENODEV;
    *device = allocate_device();
    return *device ? 0 : -ENOMEM;
}

int drmDevicesEqual(drmDevicePtr a, drmDevicePtr b)
{
    if (!a || !b || a->bustype != DRM_BUS_PLATFORM || b->bustype != DRM_BUS_PLATFORM ||
        !a->businfo.platform || !b->businfo.platform) return 0;
    return !strcmp(a->businfo.platform->fullname, b->businfo.platform->fullname);
}
