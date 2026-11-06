/* CPU writes to a disjoint byte of a read-only GPU input remain the CPU's. */
#define NATIVE_METAL_READONLY_MAIN previous_readonly_api_main
#include "native-metal-readonly-api.m"

static int untracked_input(int tracked_texture)
{
    @autoreleasepool {
        vm_address_t writable=0,readonly=0;const vm_size_t size=vm_page_size*2;
        if(!verify(vm_allocate(mach_task_self(),&writable,size,VM_FLAGS_ANYWHERE)==KERN_SUCCESS,
                   "untracked allocate CPU writable alias"))return 1;
        memset((void *)writable,0x5e,size);
        vm_prot_t current=0,maximum=0;
        if(!verify(vm_remap(mach_task_self(),&readonly,size,0,VM_FLAGS_ANYWHERE,mach_task_self(),writable,
                            FALSE,&current,&maximum,VM_INHERIT_NONE)==KERN_SUCCESS,
                   "untracked shared input alias"))return 1;
        if(!verify(vm_protect(mach_task_self(),readonly,size,TRUE,VM_PROT_READ)==KERN_SUCCESS,
                   "untracked protect GPU input alias"))return 1;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLBuffer> source=[device newBufferWithBytesNoCopy:(void *)readonly length:size
            options:tracked_texture?0:MTLResourceHazardTrackingModeUntracked deallocator:^(void *pointer,NSUInteger length){
                vm_deallocate(mach_task_self(),(vm_address_t)pointer,length);
                vm_deallocate(mach_task_self(),writable,length);
            }];
        if(!verify(source!=nil,"untracked no-copy source"))return 1;
        id<MTLBuffer> result=[device newBufferWithLength:256 options:0];
        id<MTLBuffer> work=[device newBufferWithLength:32*1024*1024 options:MTLResourceStorageModePrivate];
        if(!verify(result && work,"untracked readback and bounded GPU work"))return 1;
        _Atomic int *scheduled=calloc(1,sizeof *scheduled);if(!scheduled)return 1;
        id<MTLCommandBuffer> commands=[queue commandBuffer];
        [commands addScheduledHandler:^(id<MTLCommandBuffer> completed){
            (void)completed;
            ((unsigned char *)writable)[vm_page_size]=0x3c;
            atomic_store_explicit(scheduled,1,memory_order_release);
            puts("METALUNTRACKED CPU changed disjoint byte after scheduled input upload");
        }];
        id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
        [blit fillBuffer:work range:NSMakeRange(0,work.length) value:0x77];
        if(tracked_texture) {
            MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:32 height:2 mipmapped:NO];
            td.usage=MTLTextureUsageShaderRead;td.storageMode=MTLStorageModeShared;
            id<MTLTexture> texture=[source newTextureWithDescriptor:td offset:0 bytesPerRow:128];
            if(!verify(texture!=nil,"tracked read-only texture with disjoint CPU update"))return 1;
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(32,2,1) toBuffer:result destinationOffset:0
                destinationBytesPerRow:128 destinationBytesPerImage:256];
        } else {
            [blit copyFromBuffer:source sourceOffset:0 toBuffer:result destinationOffset:0 size:256];
        }
        [blit endEncoding];int done=complete(commands);unsigned wrong=0;
        if(done && result.contents)for(unsigned i=0;i<256;i++)wrong+=((unsigned char *)result.contents)[i]!=0x5e;
        verify(done && result.contents && wrong==0,"untracked GPU reads only its declared prefix");
        int ran=atomic_load_explicit(scheduled,memory_order_acquire);
        verify(ran && ((unsigned char *)writable)[vm_page_size]==0x3c,
               "untracked completion preserves concurrent disjoint CPU input change");
        if(ran)free(scheduled);
    }
    printf("METALUNTRACKED RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}

#ifndef NATIVE_METAL_UNTRACKED_MAIN
#define NATIVE_METAL_UNTRACKED_MAIN main
#endif
int NATIVE_METAL_UNTRACKED_MAIN(int argc,char **argv)
{
    int result=previous_readonly_api_main(argc,argv);
    if(argc!=2 || strcmp(argv[1],"graphics"))return result;
    alarm(20);atomic_store(&finished,0);pthread_t watcher;
    int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    result|=untracked_input(1);result|=untracked_input(0);
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);alarm(0);
    return result;
}
