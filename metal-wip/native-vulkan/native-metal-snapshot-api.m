/* Exercise real GPU copies and cached CPU history at a display-sized allocation. */
#define NATIVE_METAL_UNTRACKED_MAIN previous_untracked_api_main
#include "native-metal-untracked-api.m"

static int snapshot_transfers(void)
{
    @autoreleasepool {
        const NSUInteger size=12*1024*1024;
        vm_address_t original=0,destination=0;
        if(!verify(vm_allocate(mach_task_self(),&original,size,VM_FLAGS_ANYWHERE)==KERN_SUCCESS,
                   "snapshot allocate source"))return 1;
        if(!verify(vm_allocate(mach_task_self(),&destination,size,VM_FLAGS_ANYWHERE)==KERN_SUCCESS,
                   "snapshot allocate destination"))return 1;
        memset((void *)original,0x3d,size);memset((void *)destination,0,size);
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> queue=[device newCommandQueue];
        void (^release)(void *,NSUInteger)=^(void *pointer,NSUInteger length){vm_deallocate(mach_task_self(),(vm_address_t)pointer,length);};
        id<MTLBuffer> source=[device newBufferWithBytesNoCopy:(void *)original length:size options:0 deallocator:release];
        id<MTLBuffer> target=[device newBufferWithBytesNoCopy:(void *)destination length:size options:0 deallocator:release];
        if(!verify(source && target,"snapshot emulated no-copy pair"))return 1;
        void *runtime=dlopen("/redwood/lib/libmetal2vulkan.dylib",RTLD_NOW|RTLD_LOCAL);
        void (*counts)(uint64_t *)=runtime?dlsym(runtime,"nmb_host_copies"):NULL;
        if(!verify(counts!=NULL,"snapshot transfer counters available"))return 1;
        uint64_t before[4]={0},after[4]={0};
        for(unsigned round=0;round<8;round++) {
            counts(before);
            uint64_t began=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            id<MTLCommandBuffer> commands=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
            [blit copyFromBuffer:source sourceOffset:0 toBuffer:target destinationOffset:0 size:size];[blit endEncoding];
            int done=complete(commands);uint64_t elapsed=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-began;counts(after);
            unsigned wrong=0;for(NSUInteger offset=0;offset<size;offset+=1048576)wrong+=((unsigned char *)destination)[offset]!=0x3d;
            wrong+=((unsigned char *)destination)[size-1]!=0x3d;
            verify(done && wrong==0,"snapshot full-size GPU output samples");
            if(round)verify(after[0]==before[0],"snapshot unchanged CPU inputs transfer no bytes");
            printf("METALSNAPSHOT round=%u bytes=%lu commit_wait_us=%llu input_bytes=%llu output_bytes=%llu input_ns=%llu output_ns=%llu\n",
                round,(unsigned long)size,(unsigned long long)(elapsed/1000),(unsigned long long)(after[0]-before[0]),
                (unsigned long long)(after[1]-before[1]),(unsigned long long)(after[2]-before[2]),(unsigned long long)(after[3]-before[3]));
        }
        ((unsigned char *)original)[1]=0x12;((unsigned char *)original)[4097]=0x34;counts(before);
        id<MTLCommandBuffer> commands=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
        [blit copyFromBuffer:source sourceOffset:0 toBuffer:target destinationOffset:0 size:size];[blit endEncoding];
        int done=complete(commands);counts(after);
        verify(done && ((unsigned char *)destination)[1]==0x12 && ((unsigned char *)destination)[4097]==0x34,
               "snapshot sparse CPU changes reach GPU and caller");
        verify(after[0]-before[0]==2,"snapshot transfers exact two changed CPU bytes");
        commands=[queue commandBuffer];blit=[commands blitCommandEncoder];
        [blit fillBuffer:source range:NSMakeRange(16,16) value:0x77];[blit endEncoding];
        verify(complete(commands) && ((unsigned char *)original)[16]==0x77,"snapshot GPU-only output updates caller");
        counts(before);commands=[queue commandBuffer];blit=[commands blitCommandEncoder];
        [blit copyFromBuffer:source sourceOffset:0 toBuffer:target destinationOffset:0 size:size];[blit endEncoding];
        done=complete(commands);counts(after);
        verify(done && ((unsigned char *)destination)[16]==0x77 && ((unsigned char *)destination)[1]==0x12,
               "snapshot completed GPU output survives next input use");
        verify(after[0]==before[0],"snapshot GPU output is not uploaded as a new CPU change");
        if(runtime)dlclose(runtime);
    }
    printf("METALSNAPSHOT RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}

#ifndef NATIVE_METAL_SNAPSHOT_MAIN
#define NATIVE_METAL_SNAPSHOT_MAIN main
#endif
int NATIVE_METAL_SNAPSHOT_MAIN(int argc,char **argv)
{
    int result=previous_untracked_api_main(argc,argv);
    if(argc!=2 || strcmp(argv[1],"graphics"))return result;
    alarm(20);atomic_store(&finished,0);pthread_t watcher;
    int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    result|=snapshot_transfers();
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);alarm(0);
    return result;
}
