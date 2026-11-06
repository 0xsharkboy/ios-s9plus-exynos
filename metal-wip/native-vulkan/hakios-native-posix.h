/* XNU compatibility entry points. Included before Mesa's standard headers.
 * Only our virtual render node and its mapped GPU buffers are intercepted.
 * Other files/mappings use the OS. No changes to upstream Panfrost sources. */
#ifndef HAKIOS_NATIVE_POSIX_H
#define HAKIOS_NATIVE_POSIX_H
#define HAKIOS_NATIVE_DRM 1
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#ifdef __cplusplus
extern "C" {
#endif
int hakios_native_open(const char *path, int flags, ...);
int hakios_native_close(int fd);
int hakios_native_fcntl(int fd, int command, ...);
int hakios_native_stat(const char *path, struct stat *info);
void *hakios_native_mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset);
int hakios_native_munmap(void *address, size_t length);
int hakios_native_sched_setscheduler(pid_t pid, int policy, const struct sched_param *parameters);
void *hakios_native_drm_map_checked(uint32_t handle, uint64_t requested_size, size_t *mapped_size);
int hakios_native_drm_unmap_checked(uint32_t handle, void *address);
#ifdef __cplusplus
}
#endif
#ifndef HAKIOS_NATIVE_POSIX_IMPLEMENTATION
#define open(...) hakios_native_open(__VA_ARGS__)
#define close(fd) hakios_native_close(fd)
#define fcntl(...) hakios_native_fcntl(__VA_ARGS__)
#define stat(path, info) hakios_native_stat(path, info)
#define mmap(...) hakios_native_mmap(__VA_ARGS__)
#define munmap(address, length) hakios_native_munmap(address, length)
#define sched_setscheduler(pid, policy, parameters) hakios_native_sched_setscheduler(pid, policy, parameters)
#endif
#endif
