#ifndef HAKIOS_NATIVE_SYNC_H
#define HAKIOS_NATIVE_SYNC_H
#include <stdint.h>
/* The callback returns positive errno; deadline is CLOCK_MONOTONIC absolute. */
void hakios_native_sync_set_wait(int (*wait_job)(uint64_t, int64_t));
int hakios_native_sync_prepare(const uint32_t *inputs, uint32_t count, uint32_t output, void **cookie);
void hakios_native_sync_publish(void *cookie, int error, uint64_t sequence);
int hakios_native_sync_ioctl(int fd, unsigned long request, void *arg, int *handled);
void hakios_native_sync_close_fd(int fd);
#endif
