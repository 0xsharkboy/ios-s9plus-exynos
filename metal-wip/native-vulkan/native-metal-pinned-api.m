#define NATIVE_METAL_FETCH_MAIN previous_fetch_api_main
#include "native-metal-fetch-api.m"

static int pinned_pages(void)
{
    struct ReleaseProbe {_Atomic unsigned count;_Atomic int arguments;};
    struct ReleaseProbe *probe=calloc(1,sizeof *probe);
    if(!probe)return 1;
    vm_address_t original=0;
    vm_size_t size=vm_page_size;
    if(!verify(vm_allocate(mach_task_self(),&original,size,VM_FLAGS_ANYWHERE)==KERN_SUCCESS,"pin test original pages"))return 1;
    const vm_address_t remembered=original;
    for(size_t i=0;i<size/4;i++)((uint32_t *)original)[i]=0x11223344;
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLBuffer> source=[device newBufferWithBytesNoCopy:(void *)original length:size options:0
            deallocator:^(void *pointer,NSUInteger length){
                atomic_store_explicit(&probe->arguments,pointer==(void *)remembered && length==size,memory_order_relaxed);
                atomic_fetch_add_explicit(&probe->count,1,memory_order_release);
            }];
        if(!verify(source!=nil && source.contents==(void *)original,"pin preserves original contents pointer")) {
            vm_deallocate(mach_task_self(),original,size);return 1;
        }
        id<MTLBuffer> result=[device newBufferWithLength:size options:0];
        if(!verify(result!=nil,"pin readback allocation")){vm_deallocate(mach_task_self(),original,size);return 1;}
        for(size_t i=0;i<size/4;i++)((uint32_t *)original)[i]=0xaabbccdd;
        for(unsigned phase=0;phase<3;phase++) {
            if(phase==1) {
                if(!verify(vm_deallocate(mach_task_self(),original,size)==KERN_SUCCESS,"pin unmap original before next commit"))return 1;
                original=remembered;
                if(!verify(vm_allocate(mach_task_self(),&original,size,VM_FLAGS_FIXED)==KERN_SUCCESS,"pin reuse original virtual address"))return 1;
                memset((void *)original,0x33,size);
            }
            id<MTLCommandBuffer> commands=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
            if(phase==2)[blit fillBuffer:source range:NSMakeRange(0,size) value:0x5a];
            [blit copyFromBuffer:source sourceOffset:0 toBuffer:result destinationOffset:0 size:size];[blit endEncoding];
            int done=complete(commands);unsigned wrong=0;
            const uint32_t *values=result.contents;
            if(done && values)for(size_t i=0;i<size/4;i++)wrong+=values[i]!=(phase==2?0x5a5a5a5a:0xaabbccdd);
            verify(done && values && wrong==0,phase==0?"pin shares CPU writes after buffer creation":phase==1?
                "pin GPU reads original pages after address reuse":"pin GPU writes original pages after address reuse");
            if(phase) {
                unsigned changed=0;for(size_t i=0;i<size/4;i++)changed+=((uint32_t *)original)[i]!=0x33333333;
                verify(changed==0,"pin leaves replacement mapping untouched");
            }
        }
    }
    for(unsigned i=0;i<100 && !atomic_load_explicit(&probe->count,memory_order_acquire);i++)usleep(1000);
    unsigned releases=atomic_load_explicit(&probe->count,memory_order_acquire);
    verify(releases==1 && atomic_load_explicit(&probe->arguments,memory_order_relaxed),"pin releases original deallocator exactly once with original arguments");
    if(releases==1)free(probe); /* A still-pending callback keeps this tiny probe until process exit. */
    vm_deallocate(mach_task_self(),original,size);
    printf("METALPIN RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}

#ifndef NATIVE_METAL_PINNED_MAIN
#define NATIVE_METAL_PINNED_MAIN main
#endif
int NATIVE_METAL_PINNED_MAIN(int argc,char **argv)
{
    int result=previous_fetch_api_main(argc,argv);
    if(argc!=2 || strcmp(argv[1],"graphics"))return result;
    alarm(20);atomic_store(&finished,0);pthread_t watcher;int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    @autoreleasepool {result|=pinned_pages();}
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);alarm(0);
    return result;
}
