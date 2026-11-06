/* SPDX-License-Identifier: MIT
 * Give the existing IOSurface import a real descriptor identity and size.
 * PanVK's kmod import uses both drmPrimeFDToHandle and lseek; a bare surface
 * number is neither a descriptor nor a safe substitute for one.
 */
#define HAKIOS_NATIVE_POSIX_IMPLEMENTATION
#include "hakios-native-surface-fd.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define SURFACE_FD_LIMIT 256
struct surface_fd { uint64_t token,size; dev_t device; ino_t inode; uint32_t surface,handle; int anchor; };
static struct surface_fd records[SURFACE_FD_LIMIT];
static uint64_t next_token;
static pthread_mutex_t surface_fd_lock=PTHREAD_MUTEX_INITIALIZER;

int hakios_native_surface_fd_create(uint32_t surface,uint64_t size,uint64_t *token)
{
    if(!surface || !token || !size || size>(256u<<20) || (size&4095)) { errno=EINVAL;return -1; }
    *token=0;
    char path[]="/tmp/scion-surface-XXXXXX";
    int fd=mkstemp(path);if(fd<0)return -1;
    /* /tmp belongs to the guest RAM filesystem. Keep only open descriptors. */
    if(unlink(path) || fcntl(fd,F_SETFD,FD_CLOEXEC) || ftruncate(fd,(off_t)size)) {
        int saved=errno;close(fd);errno=saved;return -1;
    }
    struct stat st;
    if(fstat(fd,&st)) { int saved=errno;close(fd);errno=saved;return -1; }
    int anchor=fcntl(fd,F_DUPFD_CLOEXEC,3);
    if(anchor<0) { int saved=errno;close(fd);errno=saved;return -1; }
    pthread_mutex_lock(&surface_fd_lock);
    for(unsigned i=0;i<SURFACE_FD_LIMIT;i++)if(!records[i].token && next_token!=UINT64_MAX) {
        records[i]=(struct surface_fd){++next_token,size,st.st_dev,st.st_ino,surface,0,anchor};
        *token=records[i].token;
        pthread_mutex_unlock(&surface_fd_lock);
        return fd;
    }
    pthread_mutex_unlock(&surface_fd_lock);
    close(anchor);close(fd);errno=ENOMEM;return -1;
}

int hakios_native_surface_fd_lookup(int fd,uint32_t *surface,uint64_t *size)
{
    if(!surface || !size)return EINVAL;
    struct stat st;
    if(fstat(fd,&st))return errno;
    if(!S_ISREG(st.st_mode))return EINVAL;
    int error=EINVAL;
    pthread_mutex_lock(&surface_fd_lock);
    for(unsigned i=0;i<SURFACE_FD_LIMIT;i++)if(records[i].token && records[i].device==st.st_dev && records[i].inode==st.st_ino) {
        if(st.st_size>=0 && (uint64_t)st.st_size==records[i].size) {
            *surface=records[i].surface;*size=records[i].size;error=0;
        }
        break;
    }
    pthread_mutex_unlock(&surface_fd_lock);
    return error;
}

int hakios_native_surface_fd_release(uint64_t token)
{
    if(!token)return EINVAL;
    pthread_mutex_lock(&surface_fd_lock);
    for(unsigned i=0;i<SURFACE_FD_LIMIT;i++)if(records[i].token==token) {
        int anchor=records[i].anchor;records[i]=(struct surface_fd){0};
        pthread_mutex_unlock(&surface_fd_lock);
        return close(anchor)?errno:0;
    }
    pthread_mutex_unlock(&surface_fd_lock);
    return EINVAL;
}

int hakios_native_surface_fd_bind(int fd,uint32_t handle)
{
    if(!handle)return EINVAL;
    struct stat st;if(fstat(fd,&st))return errno;
    int error=EINVAL;
    pthread_mutex_lock(&surface_fd_lock);
    for(unsigned i=0;i<SURFACE_FD_LIMIT;i++)if(records[i].token && records[i].device==st.st_dev &&
        records[i].inode==st.st_ino && st.st_size>=0 && (uint64_t)st.st_size==records[i].size) {
        records[i].handle=handle;error=0;break;
    }
    pthread_mutex_unlock(&surface_fd_lock);return error;
}

int hakios_native_surface_fd_import(uint64_t token,uint32_t *handle,uint64_t *size)
{
    if(!token || !handle || !size)return EINVAL;
    int error=EINVAL;
    pthread_mutex_lock(&surface_fd_lock);
    for(unsigned i=0;i<SURFACE_FD_LIMIT;i++)if(records[i].token==token) {
        if(records[i].handle) {*handle=records[i].handle;*size=records[i].size;error=0;}
        else error=ENODEV;
        break;
    }
    pthread_mutex_unlock(&surface_fd_lock);return error;
}

void hakios_native_surface_fd_forget(uint32_t handle)
{
    pthread_mutex_lock(&surface_fd_lock);
    for(unsigned i=0;i<SURFACE_FD_LIMIT;i++)if(records[i].handle==handle)records[i].handle=0;
    pthread_mutex_unlock(&surface_fd_lock);
}
