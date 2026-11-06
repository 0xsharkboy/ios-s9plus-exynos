#define _GNU_SOURCE
#include "hakios-native-sync.h"
#include "xf86drm.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static atomic_uint_fast64_t completed;
static int64_t now_ns(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec; }
static int mock_gpu_wait(uint64_t sequence, int64_t deadline)
{
    while (atomic_load(&completed) < sequence) {
        if (now_ns() >= deadline) return ETIMEDOUT;
        struct timespec pause = { .tv_nsec = 100000 }; nanosleep(&pause, NULL);
    }
    return 0;
}
static void publish(uint32_t handle, uint64_t sequence)
{
    void *cookie = NULL; assert(!hakios_native_sync_prepare(NULL, 0, handle, &cookie));
    hakios_native_sync_publish(cookie, 0, sequence);
}
struct waiter { uint32_t handle; int result; atomic_bool entered, finished; };
static void *wait_thread(void *data)
{
    struct waiter *waiter = data; atomic_store(&waiter->entered, 1);
    waiter->result = drmSyncobjWait(-1, &waiter->handle, 1, now_ns() + 1000000000,
        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
    atomic_store(&waiter->finished, 1); return NULL;
}
static void pause_ms(unsigned value)
{ struct timespec t = { .tv_sec = value / 1000, .tv_nsec = (value % 1000) * 1000000 }; nanosleep(&t, NULL); }
static void *cpu_signal_thread(void *data)
{ uint32_t handle = *(uint32_t *)data; pause_ms(5); assert(!drmSyncobjSignal(-1, &handle, 1)); return NULL; }

int main(void)
{
    hakios_native_sync_set_wait(mock_gpu_wait);
    uint32_t a, b;
    assert(!drmSyncobjCreate(-1, 0, &a));
    assert(!drmSyncobjCreate(-1, DRM_SYNCOBJ_CREATE_SIGNALED, &b));
    errno=EIO;assert(drmSyncobjWait(-1, &a, 1, 0, 0, NULL) == -1 && errno==EINVAL);
    errno=EIO;assert(drmSyncobjWait(-1, &a, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL) == -1 && errno==ETIME);
    assert(!drmSyncobjWait(-1, &b, 1, 0, 0, NULL));
    struct waiter waiter = { .handle = a }; pthread_t thread;
    assert(!pthread_create(&thread, NULL, wait_thread, &waiter));
    while (!atomic_load(&waiter.entered)) pause_ms(1);
    pause_ms(5); assert(!atomic_load(&waiter.finished));
    publish(a, 10); pause_ms(5); assert(!atomic_load(&waiter.finished));
    atomic_store(&completed, 10); assert(!pthread_join(thread, NULL)); assert(!waiter.result);
    assert(!drmSyncobjReset(-1, &a, 1));
    errno=EIO;assert(drmSyncobjWait(-1, &a, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL) == -1 && errno==ETIME);
    uint32_t invalid[2] = {a, UINT32_MAX};
    assert(drmSyncobjSignal(-1, invalid, 2) < 0);
    errno=EIO;assert(drmSyncobjWait(-1, &a, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL) == -1 && errno==ETIME);
    /* Wait-any must notice a future CPU signal even while another GPU fence
     * is pending; it cannot block on that GPU fence until the full deadline. */
    publish(b, 1000);
    uint64_t point=0;
    assert(!drmSyncobjTimelineWait(-1,&b,&point,1,0,DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,NULL));
    point=1;errno=EIO;
    assert(drmSyncobjTimelineWait(-1,&b,&point,1,0,DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,NULL)==-1 && errno==ENOSYS);
    assert(!pthread_create(&thread, NULL, cpu_signal_thread, &a));
    uint32_t handles[2] = {b, a}, first = UINT32_MAX;
    assert(!drmSyncobjWait(-1, handles, 2, now_ns() + 100000000,
        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, &first));
    assert(first == 1); assert(!pthread_join(thread, NULL));
    /* Inputs need to be submitted, but not completed: the actual kernel queue
     * is ordered. Pin outputs across destruction so handles cannot alias. */
    void *cookie = NULL;
    assert(!hakios_native_sync_prepare(&b, 1, a, &cookie));
    hakios_native_sync_publish(cookie, EIO, 2000);
    assert(!drmSyncobjWait(-1, &a, 1, 0, 0, NULL));
    assert(!hakios_native_sync_prepare(NULL, 0, a, &cookie));
    assert(!drmSyncobjDestroy(-1, a));
    uint32_t reused; assert(!drmSyncobjCreate(-1, 0, &reused)); assert(reused == a);
    hakios_native_sync_publish(cookie, 0, 22);
    errno=EIO;assert(drmSyncobjWait(-1, &reused, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL) == -1 && errno==ETIME);
    /* Export/import carries an immutable job snapshot, supports >64 live
     * objects and becomes pollable only after the real completion callback. */
    int files[160]; publish(b, 50);
    for (unsigned i = 0; i < 160; ++i) assert(!drmSyncobjExportSyncFile(-1, b, &files[i]));
    assert(!drmSyncobjImportSyncFile(-1, reused, files[0]));
    publish(b, 60); atomic_store(&completed, 50);
    assert(!drmSyncobjWait(-1, &reused, 1, now_ns() + 100000000, 0, NULL));
    errno=EIO;assert(drmSyncobjWait(-1, &b, 1, 0, 0, NULL) == -1 && errno==ETIME);
    for (unsigned i = 0; i < 160; ++i) {
        struct pollfd p = {.fd = files[i], .events = POLLIN};
        assert(poll(&p, 1, 1000) == 1); assert(p.revents & POLLIN);
        hakios_native_sync_close_fd(files[i]); close(files[i]);
    }
    /* Early close of a pending file must not signal SIGPIPE or leak the
     * export into a later unrelated descriptor with the same number. */
    for (unsigned i = 0; i < 1000; ++i) {
        int file; assert(!drmSyncobjExportSyncFile(-1, b, &file));
        hakios_native_sync_close_fd(file); close(file);
    }
    assert(!drmSyncobjDestroy(-1, reused)); assert(!drmSyncobjDestroy(-1, b));
    assert(drmSyncobjTransfer(-1, reused, 1, b, 0, 0) < 0 && errno == ENOSYS);
    puts("native binary fences: wait-before-submit, CPU signal, wait-any, handle lifetime, 160 simultaneous exports, 1000 early closes PASS");
    return 0;
}
