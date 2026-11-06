/* Binary DRM fences backed by the real, ordered HakiosMali job sequence.
 * PanVK JM's unchanged vk_sync_timeline supplies Vulkan timeline semantics.
 * Never treat an unsubmitted fence as signalled. */
#include "hakios-native-sync.h"
#include "xf86drm.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct fence_object { unsigned references; bool available; uint64_t sequence; };
struct exported_fence {
    int fd, keep_read, write_fd;
    uint64_t sequence;
    bool processing, ready;
    int error;
    struct stat identity;
    struct exported_fence *next;
};
static pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static struct fence_object **objects;
static uint32_t object_capacity;
static struct exported_fence *exports;
static bool worker_started;
static int (*gpu_wait)(uint64_t, int64_t);

static int64_t monotonic_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static int sync_fail(int error, const char *function)
{
    static unsigned reported;
    if (error != ETIME && error != ETIMEDOUT &&
        __atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<64)
        fprintf(stderr,"NATIVE_SYNC_ERROR function=%s errno=%d\n",function,error);
    errno = error; return -1;
}
#define fail(error) sync_fail(error,__func__)
static struct fence_object *lookup(uint32_t handle)
{
    return handle && handle <= object_capacity ? objects[handle - 1] : NULL;
}
static void release(struct fence_object *object)
{
    if (object && !--object->references) free(object);
}
static void condition_wait_slice(int64_t deadline)
{
    int64_t left = deadline > monotonic_ns() ? deadline - monotonic_ns() : 0;
    if (left > 2000000) left = 2000000;
    if (left <= 0) return;
#ifdef __APPLE__
    struct timespec timeout = { .tv_sec = left / 1000000000, .tv_nsec = left % 1000000000 };
    pthread_cond_timedwait_relative_np(&changed, &sync_lock, &timeout);
#else
    struct timespec timeout; clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += left;
    timeout.tv_sec += timeout.tv_nsec / 1000000000;
    timeout.tv_nsec %= 1000000000;
    pthread_cond_timedwait(&changed, &sync_lock, &timeout);
#endif
}

void hakios_native_sync_set_wait(int (*callback)(uint64_t, int64_t))
{
    pthread_mutex_lock(&sync_lock); gpu_wait = callback; pthread_mutex_unlock(&sync_lock);
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
    (void)fd;
    if (!handle || flags & ~DRM_SYNCOBJ_CREATE_SIGNALED) return fail(EINVAL);
    struct fence_object *object = calloc(1, sizeof(*object));
    if (!object) return fail(ENOMEM);
    object->references = 1; object->available = flags & DRM_SYNCOBJ_CREATE_SIGNALED;
    pthread_mutex_lock(&sync_lock);
    uint32_t i;
    for (i = 0; i < object_capacity && objects[i]; ++i) {}
    if (i == object_capacity) {
        if (object_capacity > UINT32_MAX / 2) { pthread_mutex_unlock(&sync_lock); free(object); return fail(ENOMEM); }
        uint32_t new_capacity = object_capacity ? object_capacity * 2 : 64;
        struct fence_object **grown = realloc(objects, (size_t)new_capacity * sizeof(*grown));
        if (!grown) { pthread_mutex_unlock(&sync_lock); free(object); return fail(ENOMEM); }
        memset(grown + object_capacity, 0, (new_capacity - object_capacity) * sizeof(*grown));
        objects = grown; object_capacity = new_capacity;
    }
    objects[i] = object; *handle = i + 1;
    pthread_mutex_unlock(&sync_lock);
    return 0;
}

int drmSyncobjDestroy(int fd, uint32_t handle)
{
    (void)fd; pthread_mutex_lock(&sync_lock);
    struct fence_object *object = lookup(handle);
    if (!object) { pthread_mutex_unlock(&sync_lock); return fail(EINVAL); }
    objects[handle - 1] = NULL; release(object);
    pthread_mutex_unlock(&sync_lock); return 0;
}

static int change_many(const uint32_t *handles, uint32_t count, bool signal)
{
    if (count && !handles) return fail(EINVAL);
    pthread_mutex_lock(&sync_lock);
    for (uint32_t i = 0; i < count; ++i) {
        if (!lookup(handles[i])) { pthread_mutex_unlock(&sync_lock); return fail(EINVAL); }
    }
    for (uint32_t i = 0; i < count; ++i) {
        struct fence_object *object = lookup(handles[i]);
        object->available = signal; object->sequence = 0;
    }
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&sync_lock); return 0;
}
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count)
{ (void)fd; return change_many(handles, count, false); }
int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t count)
{ (void)fd; return change_many(handles, count, true); }

struct wait_snapshot { struct fence_object *object; bool available; uint64_t sequence; };
static int wait_syncs(int fd, uint32_t *handles, unsigned count, int64_t deadline, unsigned flags, uint32_t *first)
{
    (void)fd;
    const unsigned valid = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL | DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE;
    if (!count || !handles || flags & ~valid) return -EINVAL;
    struct wait_snapshot *snapshots = calloc(count, sizeof(*snapshots));
    if (!snapshots) return -ENOMEM;
    int result = 0;
    pthread_mutex_lock(&sync_lock);
    for (unsigned i = 0; i < count; ++i) {
        struct fence_object *object = lookup(handles[i]);
        if (!object) { result = -EINVAL; goto done; }
        snapshots[i].object = object; ++object->references;
        if (!object->available && !(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT)) {
            result = -EINVAL; goto done;
        }
    }
    for (;;) {
        unsigned available = 0, index = 0;
        uint64_t sequence = 0;
        bool all = flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;
        for (unsigned i = 0; i < count; ++i) {
            struct wait_snapshot *s = &snapshots[i];
            if (!s->available && s->object->available) {
                s->sequence = s->object->sequence; s->available = true;
            }
            if (!s->available) continue;
            if (!available || (all ? s->sequence > sequence : s->sequence < sequence)) {
                sequence = s->sequence; index = i;
            }
            ++available;
        }
        if (available && (!all || available == count)) {
            if ((flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) || !sequence) {
                if (first) *first = index;
                result = 0; goto done;
            }
            int (*callback)(uint64_t, int64_t) = gpu_wait;
            int64_t slice = deadline;
            int64_t now = monotonic_ns();
            if (!all && slice > now + 2000000) slice = now + 2000000;
            pthread_mutex_unlock(&sync_lock);
            int error = callback ? callback(sequence, slice) : ENODEV;
            pthread_mutex_lock(&sync_lock);
            if (!error) { if (first) *first = index; result = 0; goto done; }
            if (error != ETIME && error != ETIMEDOUT) { result = -error; goto done; }
        }
        if (deadline <= monotonic_ns()) { result = -ETIME; goto done; }
        condition_wait_slice(deadline);
    }
done:
    for (unsigned i = 0; i < count; ++i) release(snapshots[i].object);
    pthread_mutex_unlock(&sync_lock); free(snapshots); return result;
}

/* libdrm callers (Mesa's util_sync_provider included) inspect errno, not the
 * magnitude of a negative result. Keep negative errno internal to this file. */
int drmSyncobjWait(int fd, uint32_t *handles, unsigned count, int64_t deadline, unsigned flags, uint32_t *first)
{
    int error=wait_syncs(fd,handles,count,deadline,flags,first);
    return error ? fail(-error) : 0;
}

int hakios_native_sync_prepare(const uint32_t *inputs, uint32_t count, uint32_t output, void **cookie)
{
    if (!cookie || (count && !inputs)) return EINVAL;
    *cookie = NULL;
    /* All GPU chains use the same strictly ordered kernel queue. Once an
     * input fence exists its job is necessarily before this submission;
     * no CPU wait for GPU completion is needed. CPU signals are sequence 0. */
    if (count) {
        int result = wait_syncs(-1, (uint32_t *)(uintptr_t)inputs, count, INT64_MAX,
            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT | DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE, NULL);
        if (result) return -result;
    }
    if (!output) return 0;
    pthread_mutex_lock(&sync_lock);
    struct fence_object *object = lookup(output);
    if (!object) { pthread_mutex_unlock(&sync_lock); return EINVAL; }
    ++object->references; *cookie = object;
    pthread_mutex_unlock(&sync_lock); return 0;
}
void hakios_native_sync_publish(void *cookie, int error, uint64_t sequence)
{
    if (!cookie) return;
    pthread_mutex_lock(&sync_lock);
    struct fence_object *object = cookie;
    if (!error) { object->available = true; object->sequence = sequence; pthread_cond_broadcast(&changed); }
    release(object); pthread_mutex_unlock(&sync_lock);
}

static void cleanup_export(struct exported_fence *file)
{
    if (file->keep_read >= 0) close(file->keep_read);
    if (file->write_fd >= 0) close(file->write_fd);
    free(file);
}
static void *fence_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&sync_lock);
    for (;;) {
        struct exported_fence *file = NULL;
        for (struct exported_fence *p = exports; p; p = p->next)
            if (!p->ready && !p->processing && (!file || p->sequence < file->sequence)) file = p;
        if (!file) { pthread_cond_wait(&changed, &sync_lock); continue; }
        file->processing = true;
        int (*callback)(uint64_t, int64_t) = gpu_wait;
        pthread_mutex_unlock(&sync_lock);
        int error = file->sequence ? (callback ? callback(file->sequence, monotonic_ns() + 2000000) : ENODEV) : 0;
        pthread_mutex_lock(&sync_lock);
        file->processing = false;
        if (error == ETIMEDOUT || error == ETIME) continue;
        file->error = error; file->ready = true;
        /* keep_read holds a reader until after this write: closing the public
         * descriptor early cannot cause SIGPIPE. A ready fd is pollable. */
        const char value = 1;
        if (write(file->write_fd, &value, 1) != 1 && !file->error) file->error = EIO;
        close(file->write_fd); file->write_fd = -1;
        close(file->keep_read); file->keep_read = -1;
        if (file->fd < 0) {
            struct exported_fence **p = &exports;
            while (*p != file) p = &(*p)->next;
            *p = file->next; cleanup_export(file);
        }
        pthread_cond_broadcast(&changed);
    }
    return NULL;
}

int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *out_fd)
{
    (void)fd;
    if (!out_fd) return fail(EINVAL);
    struct exported_fence *file = calloc(1, sizeof(*file));
    if (!file) return fail(ENOMEM);
    int pipe_fds[2];
    if (pipe(pipe_fds)) { free(file); return -1; }
    file->fd = pipe_fds[0]; file->write_fd = pipe_fds[1];
    file->keep_read = dup(file->fd);
    if (file->keep_read < 0 || fcntl(file->fd, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(file->keep_read, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(file->write_fd, F_SETFD, FD_CLOEXEC) < 0 || fstat(file->fd, &file->identity)) {
        int saved = errno; close(file->fd); cleanup_export(file); return fail(saved);
    }
    pthread_mutex_lock(&sync_lock);
    struct fence_object *object = lookup(handle);
    if (!object || !object->available) {
        pthread_mutex_unlock(&sync_lock); close(file->fd); cleanup_export(file); return fail(EINVAL);
    }
    file->sequence = object->sequence;
    if (!worker_started) {
        pthread_t thread;
        int error = pthread_create(&thread, NULL, fence_worker, NULL);
        if (error) { pthread_mutex_unlock(&sync_lock); close(file->fd); cleanup_export(file); return fail(error); }
        pthread_detach(thread); worker_started = true;
    }
    file->next = exports; exports = file; *out_fd = file->fd;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&sync_lock); return 0;
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int source_fd)
{
    (void)fd;
    struct stat identity;
    if (fstat(source_fd, &identity)) return -1;
    pthread_mutex_lock(&sync_lock);
    struct fence_object *object = lookup(handle);
    struct exported_fence *file = exports;
    while (file && (file->fd != source_fd || file->identity.st_dev != identity.st_dev ||
        file->identity.st_ino != identity.st_ino)) file = file->next;
    if (!object || !file || file->error) {
        int error = file && file->error ? file->error : EINVAL;
        pthread_mutex_unlock(&sync_lock); return fail(error);
    }
    object->sequence = file->sequence; object->available = true;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&sync_lock); return 0;
}

void hakios_native_sync_close_fd(int fd)
{
    pthread_mutex_lock(&sync_lock);
    struct exported_fence **p = &exports;
    while (*p && (*p)->fd != fd) p = &(*p)->next;
    if (*p) {
        struct exported_fence *file = *p; file->fd = -1;
        if (!file->processing) { *p = file->next; cleanup_export(file); }
    }
    pthread_mutex_unlock(&sync_lock);
}

/* No kernel timeline or cross-process opaque-fd objects are advertised.
 * Nonzero timeline points cannot be silently mapped to binary fences. */
int drmSyncobjFDToHandle(int fd, int source_fd, uint32_t *handle)
{ (void)fd; (void)source_fd; (void)handle; return fail(ENOSYS); }
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *out_fd)
{ (void)fd; (void)handle; (void)out_fd; return fail(ENOSYS); }
int drmSyncobjTimelineSignal(int fd, const uint32_t *handles, uint64_t *points, uint32_t count)
{ (void)fd; (void)handles; (void)points; (void)count; return fail(ENOSYS); }
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned count, int64_t deadline, unsigned flags, uint32_t *first)
{
    if(count && !points) return fail(EINVAL);
    for(unsigned i=0;i<count;i++) if(points[i]) return fail(ENOSYS);
    /* Mesa uses timeline WAIT_AVAILABLE even for binary syncobjs. Zero points
     * name those binary objects; no native timeline values are advertised. */
    return drmSyncobjWait(fd,handles,count,deadline,flags,first);
}
int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points, uint32_t count, uint32_t flags)
{ (void)fd; (void)handles; (void)points; (void)count; (void)flags; return fail(ENOSYS); }
int drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points, uint32_t count)
{ return drmSyncobjQuery2(fd, handles, points, count, 0); }
int drmSyncobjTransfer(int fd, uint32_t dst, uint64_t dst_point, uint32_t src, uint64_t src_point, uint32_t flags)
{
    (void)fd;
    if (dst_point || src_point || flags) return fail(ENOSYS);
    pthread_mutex_lock(&sync_lock);
    struct fence_object *target = lookup(dst), *source = lookup(src);
    if (!target || !source || !source->available) { pthread_mutex_unlock(&sync_lock); return fail(EINVAL); }
    target->available = true; target->sequence = source->sequence;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&sync_lock); return 0;
}

int hakios_native_sync_ioctl(int fd, unsigned long request, void *arg, int *handled)
{
    *handled = 1;
    if (!arg) return fail(EINVAL);
    if (request == DRM_IOCTL_SYNCOBJ_CREATE) {
        struct drm_syncobj_create *r = arg; return drmSyncobjCreate(fd, r->flags, &r->handle);
    }
    if (request == DRM_IOCTL_SYNCOBJ_DESTROY) {
        struct drm_syncobj_destroy *r = arg; return drmSyncobjDestroy(fd, r->handle);
    }
    if (request == DRM_IOCTL_SYNCOBJ_RESET || request == DRM_IOCTL_SYNCOBJ_SIGNAL) {
        struct drm_syncobj_array *r = arg;
        return request == DRM_IOCTL_SYNCOBJ_RESET ? drmSyncobjReset(fd, (const uint32_t *)(uintptr_t)r->handles, r->count_handles) :
            drmSyncobjSignal(fd, (const uint32_t *)(uintptr_t)r->handles, r->count_handles);
    }
    if (request == DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD) {
        struct drm_syncobj_handle *r = arg;
        return r->flags == DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE ?
            drmSyncobjExportSyncFile(fd, r->handle, &r->fd) : fail(ENOSYS);
    }
    if (request == DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE) {
        struct drm_syncobj_handle *r = arg;
        return r->flags == DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE ?
            drmSyncobjImportSyncFile(fd, r->handle, r->fd) : fail(ENOSYS);
    }
    if (request == DRM_IOCTL_SYNCOBJ_WAIT) {
        struct drm_syncobj_wait *r = arg;
        return drmSyncobjWait(fd, (uint32_t *)(uintptr_t)r->handles, r->count_handles,
            r->timeout_nsec, r->flags, &r->first_signaled);
    }
    *handled = 0; return 0;
}
