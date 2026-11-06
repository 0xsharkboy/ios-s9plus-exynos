/* SPDX-License-Identifier: MIT */
#include "hakios-native-surface-fd.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static void *parallel(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<64;i++) {
        uint64_t token,size;uint32_t surface;
        int fd=hakios_native_surface_fd_create(71,16384,&token);assert(fd>=0);
        assert(!hakios_native_surface_fd_lookup(fd,&surface,&size) && surface==71 && size==16384);
        close(fd);assert(!hakios_native_surface_fd_release(token));
    }
    return NULL;
}
int main(void)
{
    uint64_t token=99,size;uint32_t surface;
    assert(hakios_native_surface_fd_create(0,16384,&token)<0);
    assert(hakios_native_surface_fd_create(5,16385,&token)<0);
    assert(hakios_native_surface_fd_create(5,0,&token)<0);
    assert(hakios_native_surface_fd_create(5,(256u<<20)+4096u,&token)<0);
    int fd=hakios_native_surface_fd_create(5,16384,&token);assert(fd>=0 && token);
    assert(lseek(fd,0,SEEK_END)==16384 && (fcntl(fd,F_GETFD)&FD_CLOEXEC));
    assert(!hakios_native_surface_fd_lookup(fd,&surface,&size) && surface==5 && size==16384);
    uint32_t imported=0;
    assert(hakios_native_surface_fd_import(token,&imported,&size)==ENODEV);
    assert(!hakios_native_surface_fd_bind(fd,77));
    assert(!hakios_native_surface_fd_import(token,&imported,&size) && imported==77 && size==16384);
    hakios_native_surface_fd_forget(77);
    assert(hakios_native_surface_fd_import(token,&imported,&size)==ENODEV);
    assert(!hakios_native_surface_fd_bind(fd,78));
    int duplicate=dup(fd);assert(duplicate>=0);close(fd);
    assert(!hakios_native_surface_fd_lookup(duplicate,&surface,&size));
    int unrelated=open("/dev/null",O_RDWR);assert(unrelated>=0);
    assert(hakios_native_surface_fd_lookup(unrelated,&surface,&size)!=0);
    close(unrelated);
    assert(ftruncate(duplicate,32768)==0);
    assert(hakios_native_surface_fd_lookup(duplicate,&surface,&size)!=0);
    assert(ftruncate(duplicate,16384)==0);
    assert(!hakios_native_surface_fd_release(token));
    assert(hakios_native_surface_fd_import(token,&imported,&size)!=0);
    assert(hakios_native_surface_fd_lookup(duplicate,&surface,&size)!=0);
    assert(hakios_native_surface_fd_release(token)!=0);close(duplicate);
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,parallel,NULL));
    for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    int descriptors[256];uint64_t tokens[256];
    for(unsigned i=0;i<256;i++)assert((descriptors[i]=hakios_native_surface_fd_create(i+1,16384,&tokens[i]))>=0);
    assert(hakios_native_surface_fd_create(1000,16384,&token)<0 && errno==ENOMEM);
    for(unsigned i=0;i<256;i++){close(descriptors[i]);assert(!hakios_native_surface_fd_release(tokens[i]));}
    fd=hakios_native_surface_fd_create(9,16384,&token);assert(fd>=0);close(fd);
    assert(!hakios_native_surface_fd_release(token));
    puts("PASS surface FD: seekable size, duplicate identity, unrelated/recycled descriptor rejection, mutation, release, concurrency and bounded capacity");
}
