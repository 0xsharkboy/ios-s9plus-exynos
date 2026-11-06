/* Exercise iOS Metal discovery, public-fixture shaders and IOSurface rendering.
 * The core's original objects/blits tests remain available in the same process name.
 */
#define main scion_metal_test_main
#include "metal_test.m"
#undef main
#include <IOSurface/IOSurfaceRef.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>

static _Atomic int finished;
static void *watch_memory(void *unused)
{
    (void)unused;
    while (!atomic_load(&finished)) {
        mach_task_basic_info_data_t info;
        mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) == KERN_SUCCESS &&
            info.resident_size > 500u*1024u*1024u) {
            fputs("METALAPI FAIL memory ceiling\n", stderr);
            _exit(90);
        }
        usleep(20000);
    }
    return NULL;
}

static int checks, failures;
static void bridge_error(void)
{
    void *handle=dlopen("/redwood/lib/libmetal2vulkan.dylib",RTLD_NOW|RTLD_LOCAL);
    const char *(*last)(void)=handle ? dlsym(handle,"nmb_last_error") : NULL;
    printf("METALAPI bridge error=%s\n",last ? last() : "unavailable");
}
static int verify(int ok, const char *name)
{
    printf("METALAPI %s %s\n", ok ? "PASS" : "FAIL", name);
    checks++; failures += !ok;
    return ok;
}

static int complete(id<MTLCommandBuffer> commands)
{
    [commands commit];
    [commands waitUntilCompleted];
    if (commands.status != MTLCommandBufferStatusCompleted)
        printf("METALAPI command status=%lu error=%s\n", (unsigned long)commands.status,
            commands.error.description.UTF8String);
    return commands.status == MTLCommandBufferStatusCompleted;
}

static unsigned wrong_pixels(const unsigned char *bytes, size_t row, unsigned side)
{
    unsigned wrong = 0;
    for (unsigned y=0; y<side; y++) for (unsigned x=0; x<side; x++) {
        const unsigned char *p = bytes+y*row+x*4;
        const unsigned char inside[] = {255,51,0,255}, outside[] = {0,0,255,255};
        wrong += memcmp(p, x<side/2 && y<side/2 ? inside : outside, 4) != 0;
    }
    return wrong;
}

static int draw(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLRenderPipelineState> state,
                id<MTLTexture> target, id<MTLBuffer> readback, unsigned side)
{
    const float positions[6][2] = {{-1,1},{0,1},{-1,0},{0,1},{0,0},{-1,0}};
    const float colors[6][4] = {{1,.2,0,1},{1,.2,0,1},{1,.2,0,1},{1,.2,0,1},{1,.2,0,1},{1,.2,0,1}};
    id<MTLBuffer> pb=[device newBufferWithBytes:positions length:sizeof positions options:0];
    id<MTLBuffer> cb=[device newBufferWithBytes:colors length:sizeof colors options:0];
    if (!pb || !cb || !target || !readback || !state) return 0;
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=target;
    pass.colorAttachments[0].loadAction=MTLLoadActionClear;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,1,1);
    id<MTLCommandBuffer> commands=[queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder=[commands renderCommandEncoderWithDescriptor:pass];
    if (!encoder) return 0;
    [encoder setRenderPipelineState:state];
    [encoder setVertexBuffer:pb offset:0 atIndex:0];
    [encoder setVertexBuffer:cb offset:0 atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder endEncoding];
    id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
    [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(side,side,1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:side*4 destinationBytesPerImage:side*side*4];
    [blit endEncoding];
    return complete(commands);
}

static int graphics(void)
{
    NSError *error=nil;
    struct stat st;
    const char *qc="/System/Library/Frameworks/QuartzCore.framework/default.metallib";
    printf("METALAPI QuartzCore metallib bytes=%lld\n", stat(qc,&st)==0 ? (long long)st.st_size : -1LL);
    FILE *journal=fopen("/var/tmp/com.apple.backboardd/m2v/pipelines.log","r");
    if(journal) {
        char line[4096];unsigned lines=0;
        fseek(journal,0,SEEK_END);long size=ftell(journal);
        fseek(journal,size>65536?size-65536:0,SEEK_SET);
        while(lines++<256 && fgets(line,sizeof line,journal)) printf("METALJOURNAL %s",line);
        fclose(journal);
    }
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if (!verify(device!=nil,"iOS MTLCreateSystemDefaultDevice")) return 1;
    printf("METALAPI device=%s registry=%llu\n", device.name.UTF8String,(unsigned long long)device.registryID);
    id<MTLLibrary> quartz=[device newLibraryWithURL:[NSURL fileURLWithPath:@(qc)] error:&error];
    verify(quartz!=nil,"load installed QuartzCore metallib");
    printf("METALAPI QuartzCore functions=%lu error=%s\n",(unsigned long)quartz.functionNames.count,error.description.UTF8String);
    id<MTLLibrary> library=[device newLibraryWithURL:[NSURL fileURLWithPath:@"/redwood/share/public-runtime.metallib"] error:&error];
    if (!verify(library!=nil,"load public fixture through Metal API")) {
        printf("METALAPI library error=%s\n",error.description.UTF8String); return 1;
    }
    printf("METALAPI fixture functions=%s\n",[library.functionNames componentsJoinedByString:@" "].UTF8String);
    id<MTLCommandQueue> queue=[device newCommandQueue];
    if (!verify(queue!=nil,"Metal command queue")) return 1;
    id<MTLFunction> computeFunction=[library newFunctionWithName:@"scale_and_offset"];
    if (!verify(computeFunction!=nil,"Metal compute function")) {bridge_error();return 1;}
    id<MTLComputePipelineState> compute=[device newComputePipelineStateWithFunction:computeFunction error:&error];
    if (!verify(compute!=nil,"Metal compute pipeline")) {
        printf("METALAPI compute error=%s\n",error.description.UTF8String);bridge_error();return 1;
    }
    enum {COUNT=1000,SIDE=64};
    float input[COUNT],scale=2;
    for (unsigned i=0;i<COUNT;i++) input[i]=(float)i;
    id<MTLBuffer> in=[device newBufferWithBytes:input length:sizeof input options:0];
    id<MTLBuffer> out=[device newBufferWithLength:sizeof input options:0];
    if (!verify(in && out,"Metal compute buffers")) return 1;
    id<MTLCommandBuffer> commands=[queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder=[commands computeCommandEncoder];
    [encoder setComputePipelineState:compute];
    [encoder setBuffer:in offset:0 atIndex:0];
    [encoder setBuffer:out offset:0 atIndex:1];
    [encoder setBytes:&scale length:sizeof scale atIndex:2];
    [encoder dispatchThreads:MTLSizeMake(COUNT,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
    [encoder endEncoding];
    int done=complete(commands); unsigned wrong=0;
    const float *values=out.contents;
    if (done && values) for(unsigned i=0;i<COUNT;i++) wrong+=values[i] != 3.0f*i;
    verify(done && values && wrong==0,"Metal compute 1000 exact results");
    MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];
    pd.vertexFunction=[library newFunctionWithName:@"colored_vertex"];
    pd.fragmentFunction=[library newFunctionWithName:@"colored_fragment"];
    pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> state=[device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!verify(state!=nil,"Metal graphics pipeline")) {
        printf("METALAPI pipeline error=%s\n",error.description.UTF8String); return 1;
    }
    MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:SIDE height:SIDE mipmapped:NO];
    td.usage=MTLTextureUsageRenderTarget;
    td.storageMode=MTLStorageModePrivate;
    id<MTLTexture> texture=[device newTextureWithDescriptor:td];
    id<MTLBuffer> readback=[device newBufferWithLength:SIDE*SIDE*4 options:0];
    done=draw(device,queue,state,texture,readback,SIDE);
    verify(done && readback.contents && wrong_pixels(readback.contents,SIDE*4,SIDE)==0,"Metal render and readback 4096 exact pixels");
    NSDictionary *props=@{(id)kIOSurfaceWidth:@(SIDE),(id)kIOSurfaceHeight:@(SIDE),
        (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@(SIDE*4),
        (id)kIOSurfaceAllocSize:@(SIDE*SIDE*4),(id)kIOSurfacePixelFormat:@(0x52474241)};
    IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (verify(surface!=NULL,"IOSurface allocation")) {
        td.storageMode=MTLStorageModeShared;
        id<MTLTexture> shared=[device newTextureWithDescriptor:td iosurface:surface plane:0];
        if (verify(shared!=nil,"Metal texture over IOSurface")) {
            done=draw(device,queue,state,shared,readback,SIDE);
            verify(done && readback.contents && wrong_pixels(readback.contents,SIDE*4,SIDE)==0,"IOSurface render Metal readback");
            IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
            verify(done && IOSurfaceGetBaseAddress(surface) && wrong_pixels(IOSurfaceGetBaseAddress(surface),IOSurfaceGetBytesPerRow(surface),SIDE)==0,"IOSurface CPU visibility");
            IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
        }
        CFRelease(surface);
    }
    printf("METALAPI RESULT checks=%d failures=%d\n",checks,failures);
    return failures ? 1 : 0;
}

#ifndef NATIVE_METAL_API_MAIN
#define NATIVE_METAL_API_MAIN main
#endif
int NATIVE_METAL_API_MAIN(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);
    setenv("MESA_LOG","file",1);setenv("MESA_LOG_LEVEL","debug",1);setenv("PANVK_DEBUG","startup",1);
    if (argc!=2 || strcmp(argv[1],"graphics")) return scion_metal_test_main(argc,argv);
    struct sigaction sa={0};sa.sa_sigaction=fault;sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,NULL);sigaction(SIGBUS,&sa,NULL);sigaction(SIGILL,&sa,NULL);sigaction(SIGABRT,&sa,NULL);
    alarm(20);
    pthread_t watcher;int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    int result;
    @autoreleasepool {result=graphics();}
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);
    alarm(0);return result;
}
