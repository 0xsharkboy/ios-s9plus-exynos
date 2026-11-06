/* Valid read-only Metal inputs must survive native image layout transitions. */
#define NATIVE_METAL_PINNED_MAIN previous_pinned_api_main
#include "native-metal-pinned-api.m"

static int readonly_inputs(void)
{
    @autoreleasepool {
        vm_address_t original=0;
        const vm_size_t size=vm_page_size;
        if(!verify(vm_allocate(mach_task_self(),&original,size,VM_FLAGS_ANYWHERE)==KERN_SUCCESS,
                   "readonly allocate input pages"))return 1;
        memset((void *)original,0x5e,size);
        if(!verify(vm_protect(mach_task_self(),original,size,TRUE,VM_PROT_READ)==KERN_SUCCESS,
                   "readonly set current and maximum protection")) {
            vm_deallocate(mach_task_self(),original,size);return 1;
        }
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLBuffer> source=[device newBufferWithBytesNoCopy:(void *)original length:size options:0
            deallocator:^(void *pointer,NSUInteger length){vm_deallocate(mach_task_self(),(vm_address_t)pointer,length);}];
        if(!verify(source!=nil,"readonly no-copy input buffer")) {
            vm_deallocate(mach_task_self(),original,size);return 1;
        }
        id<MTLBuffer> result=[device newBufferWithLength:size options:0];
        if(!verify(result!=nil,"readonly readback buffer"))return 1;
        id<MTLCommandBuffer> commands=[queue commandBuffer];
        id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
        [blit copyFromBuffer:source sourceOffset:0 toBuffer:result destinationOffset:0 size:size];
        [blit endEncoding];
        int done=complete(commands);unsigned wrong=0;
        if(done && result.contents)for(size_t i=0;i<size;i++)wrong+=((unsigned char *)result.contents)[i]!=0x5e;
        verify(done && result.contents && wrong==0,"readonly buffer GPU read and completion");
        for(unsigned padding=0;padding<2;padding++) {
            const NSUInteger width=32,height=16,row=padding?256:128;
            MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:width height:height mipmapped:NO];
            td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageUnknown;
            id<MTLTexture> texture=[source newTextureWithDescriptor:td offset:0 bytesPerRow:row];
            if(!verify(texture!=nil,padding?"readonly padded buffer texture":"readonly packed buffer texture"))continue;
            for(unsigned round=0;round<2;round++) {
                commands=[queue commandBuffer];blit=[commands blitCommandEncoder];
                [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(width,height,1) toBuffer:result destinationOffset:0
                    destinationBytesPerRow:width*4 destinationBytesPerImage:width*height*4];
                [blit endEncoding];done=complete(commands);wrong=0;
                if(done && result.contents)for(size_t i=0;i<width*height*4;i++)wrong+=((unsigned char *)result.contents)[i]!=0x5e;
                printf("METALREADONLY padding=%u round=%u wrong=%u completed=%d\n",padding,round,wrong,done);
                verify(done && result.contents && wrong==0,padding?
                    "readonly padded texture GPU read and completion":"readonly packed texture GPU read and completion");
                wrong=0;for(size_t i=0;i<size;i++)wrong+=((const unsigned char *)original)[i]!=0x5e;
                verify(wrong==0,"readonly original texels and padding unchanged");
            }
        }
    }
    printf("METALREADONLY RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}

#ifndef NATIVE_METAL_READONLY_MAIN
#define NATIVE_METAL_READONLY_MAIN main
#endif
int NATIVE_METAL_READONLY_MAIN(int argc,char **argv)
{
    int result=previous_pinned_api_main(argc,argv);
    if(argc!=2 || strcmp(argv[1],"graphics"))return result;
    alarm(20);atomic_store(&finished,0);pthread_t watcher;
    int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    result|=readonly_inputs();
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);alarm(0);
    return result;
}
