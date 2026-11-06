/* Retain all previously verified Metal API tests, then exercise authored framebuffer fetch. */
#define NATIVE_METAL_API_MAIN previous_metal_api_main
#include "native-metal-api.m"
#include <time.h>

static int fetch_graphics(unsigned width,unsigned height)
{
    NSError *error=nil;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    id<MTLLibrary> library=[device newLibraryWithURL:[NSURL fileURLWithPath:@"/redwood/share/public-runtime.metallib"] error:&error];
    id<MTLCommandQueue> queue=[device newCommandQueue];
    MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];
    pd.vertexFunction=[library newFunctionWithName:@"colored_vertex"];
    pd.fragmentFunction=[library newFunctionWithName:@"fetch_scalar"];
    pd.colorAttachments[0].pixelFormat=MTLPixelFormatR32Float;
    id<MTLRenderPipelineState> state=[device newRenderPipelineStateWithDescriptor:pd error:&error];
    if(!verify(state!=nil,"serialized fetch Metal pipeline")) {
        printf("METALFETCH pipeline error=%s\n",error.description.UTF8String);bridge_error();return 1;
    }
    float positions[12][2],colors[12][4];
    const float triangle[3][2]={{-1,-1},{3,-1},{-1,3}};
    for(unsigned i=0;i<12;i++) {memcpy(positions[i],triangle[i%3],sizeof triangle[0]);for(unsigned c=0;c<4;c++)colors[i][c]=1;}
    id<MTLBuffer> pb=[device newBufferWithBytes:positions length:sizeof positions options:0];
    id<MTLBuffer> cb=[device newBufferWithBytes:colors length:sizeof colors options:0];
    id<MTLBuffer> readback=[device newBufferWithLength:(NSUInteger)width*height*4 options:0];
    MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:width height:height mipmapped:NO];
    td.usage=MTLTextureUsageRenderTarget;td.storageMode=MTLStorageModePrivate;
    id<MTLTexture> texture=[device newTextureWithDescriptor:td];
    if(!verify(texture && readback && pb && cb,"serialized fetch resources"))return 1;
    int all_results=0;
    for(unsigned round=0;round<4;round++) {
    uint64_t encode_start=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=texture;pass.colorAttachments[0].loadAction=MTLLoadActionClear;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;pass.colorAttachments[0].clearColor=MTLClearColorMake(1,1,1,1);
    id<MTLCommandBuffer> commands=[queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder=[commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:state];[encoder setVertexBuffer:pb offset:0 atIndex:0];[encoder setVertexBuffer:cb offset:0 atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [encoder setScissorRect:(MTLScissorRect){0,0,width/2,height}];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:6 vertexCount:6];
    [encoder endEncoding];
    id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:width*4 destinationBytesPerImage:(NSUInteger)width*height*4];
    [blit endEncoding];
    uint64_t start=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    int done=complete(commands);uint64_t wait_end=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    unsigned wrong=0;const float *pixels=readback.contents;
    if(done && pixels)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)wrong+=pixels[(size_t)y*width+x]!=(x<width/2?.0625f:.25f);
    printf("METALFETCH round=%u size=%ux%u wrong=%u/%u completed=%d encode_us=%llu submit_wait_us=%llu verify_us=%llu\n",
        round,width,height,wrong,width*height,done,(unsigned long long)((start-encode_start)/1000),
        (unsigned long long)((wait_end-start)/1000),(unsigned long long)((clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-wait_end)/1000));
    all_results |= !verify(done && pixels && wrong==0,"serialized fetch four overlapping triangles and loadOp clear");
    }
    return all_results;
}

#ifndef NATIVE_METAL_FETCH_MAIN
#define NATIVE_METAL_FETCH_MAIN main
#endif
int NATIVE_METAL_FETCH_MAIN(int argc,char **argv)
{
    if(argc!=2 || strcmp(argv[1],"graphics"))return previous_metal_api_main(argc,argv);
    setvbuf(stdout,NULL,_IONBF,0);setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);
    setenv("MESA_LOG","file",1);setenv("MESA_LOG_LEVEL","debug",1);setenv("PANVK_DEBUG","startup",1);
    struct sigaction sa={0};sa.sa_sigaction=fault;sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,NULL);sigaction(SIGBUS,&sa,NULL);sigaction(SIGILL,&sa,NULL);sigaction(SIGABRT,&sa,NULL);
    alarm(20);atomic_store(&finished,0);pthread_t watcher;int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    int result;
    @autoreleasepool {result=graphics();result|=fetch_graphics(64,64);result|=fetch_graphics(1188,2442);}
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);alarm(0);
    printf("METALFETCH RESULT checks=%d failures=%d\n",checks,failures);
    return result || failures?1:0;
}
