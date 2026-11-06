/* Authored checks: deferred Metal compilation, snapshots and callback reentrancy. */
#define NATIVE_METAL_SNAPSHOT_MAIN previous_snapshot_api_main
#include "native-metal-snapshot-api.m"
#include <dispatch/dispatch.h>

static int callback_wait(dispatch_semaphore_t semaphore)
{return dispatch_semaphore_wait(semaphore,dispatch_time(DISPATCH_TIME_NOW,8*NSEC_PER_SEC))==0;}

static void async_compilation(void)
{
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();NSError *error=nil;
    id<MTLLibrary> library=[device newLibraryWithURL:[NSURL fileURLWithPath:@"/redwood/share/public-runtime.metallib"] error:&error];
    if(!verify(device && library,"async Metal device and authored library"))return;
    pthread_t caller=pthread_self();
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    __block BOOL offThread=NO;
    __block id<MTLFunction> specialized=nil;
    __block NSError *failure=nil;
    uint64_t began=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    [library newFunctionWithName:@"scale_and_offset" constantValues:[MTLFunctionConstantValues new]
        completionHandler:^(id<MTLFunction> function,NSError *result){
            offThread=!pthread_equal(caller,pthread_self());specialized=function;failure=result;
            dispatch_semaphore_signal(done);
        }];
    printf("METALASYNC function_enqueue_us=%llu\n",(unsigned long long)((clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-began)/1000));
    int waited=callback_wait(done);
    verify(waited && specialized && !failure,"async specialized function completes successfully");
    verify(waited && offThread,"async function callback runs off caller thread");

    done=dispatch_semaphore_create(0);offThread=NO;failure=nil;
    [library newFunctionWithName:@"authored_missing_function" constantValues:[MTLFunctionConstantValues new]
        completionHandler:^(id<MTLFunction> function,NSError *result){
            offThread=!pthread_equal(caller,pthread_self());failure=result;
            if(function)failure=nil;
            dispatch_semaphore_signal(done);
        }];
    waited=callback_wait(done);
    verify(waited && failure && offThread,"async missing function reports an error off caller thread");

    id<MTLFunction> vertex=[library newFunctionWithName:@"colored_vertex"];
    id<MTLFunction> fragment=[library newFunctionWithName:@"colored_fragment"];
    id<MTLFunction> compute=[library newFunctionWithName:@"scale_and_offset"];
    for(unsigned options=0;options<2;options++) {
        MTLRenderPipelineDescriptor *descriptor=[MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction=vertex;descriptor.fragmentFunction=fragment;
        descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        done=dispatch_semaphore_create(0);offThread=NO;failure=nil;
        __block id<MTLRenderPipelineState> state=nil;
        void (^completed)(id<MTLRenderPipelineState>,NSError *)=^(id<MTLRenderPipelineState> result,NSError *problem){
            state=result;failure=problem;offThread=!pthread_equal(caller,pthread_self());dispatch_semaphore_signal(done);
        };
        began=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if(options)[device newRenderPipelineStateWithDescriptor:descriptor options:MTLPipelineOptionNone
            completionHandler:^(id<MTLRenderPipelineState> result,MTLRenderPipelineReflection *reflection,NSError *problem){
                (void)reflection;completed(result,problem);
            }];
        else [device newRenderPipelineStateWithDescriptor:descriptor completionHandler:completed];
        printf("METALASYNC render_options=%u enqueue_us=%llu\n",options,
            (unsigned long long)((clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-began)/1000));
        descriptor.vertexFunction=nil;descriptor.fragmentFunction=nil;descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatInvalid;
        descriptor=nil;
        waited=callback_wait(done);
        verify(waited && state && !failure,"async render retains submission-time descriptor");
        verify(waited && offThread,"async render callback runs off caller thread");
        if(waited && state) {
            MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];
            td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget;
            id<MTLBuffer> readback=[device newBufferWithLength:64*64*4 options:0];
            int rendered=draw(device,[device newCommandQueue],state,[device newTextureWithDescriptor:td],readback,64);
            verify(rendered && readback.contents && wrong_pixels(readback.contents,64*4,64)==0,
                "async render pipeline draws 4096 exact pixels");
        }
    }
    for(unsigned options=0;options<2;options++) {
        done=dispatch_semaphore_create(0);offThread=NO;failure=nil;
        __block id<MTLComputePipelineState> state=nil;
        void (^completed)(id<MTLComputePipelineState>,NSError *)=^(id<MTLComputePipelineState> result,NSError *problem){
            state=result;failure=problem;offThread=!pthread_equal(caller,pthread_self());dispatch_semaphore_signal(done);
        };
        if(options)[device newComputePipelineStateWithFunction:compute options:MTLPipelineOptionNone
            completionHandler:^(id<MTLComputePipelineState> result,MTLComputePipelineReflection *reflection,NSError *problem){
                (void)reflection;completed(result,problem);
            }];
        else [device newComputePipelineStateWithFunction:compute completionHandler:completed];
        waited=callback_wait(done);
        verify(waited && state && !failure && offThread,"async compute pipeline completes off caller thread");
    }
    done=dispatch_semaphore_create(0);
    __block BOOL nestedOK=NO;
    [device newComputePipelineStateWithFunction:compute completionHandler:^(id<MTLComputePipelineState> outer,NSError *outerError){
        dispatch_semaphore_t nested=dispatch_semaphore_create(0);
        __block BOOL innerOK=NO;
        [device newComputePipelineStateWithFunction:compute completionHandler:^(id<MTLComputePipelineState> inner,NSError *innerError){
            innerOK=inner && !innerError;dispatch_semaphore_signal(nested);
        }];
        nestedOK=outer && !outerError && callback_wait(nested) && innerOK;
        dispatch_semaphore_signal(done);
    }];
    waited=callback_wait(done);
    verify(waited && nestedOK,"async callback may wait for another compilation without blocking compiler");
    printf("METALASYNC RESULT checks=%d failures=%d\n",checks,failures);
}

int main(int argc,char **argv)
{
    int result=previous_snapshot_api_main(argc,argv);
    if(argc!=2 || strcmp(argv[1],"graphics"))return result;
    alarm(30);
    @autoreleasepool {async_compilation();}
    alarm(0);
    return result || failures?1:0;
}
