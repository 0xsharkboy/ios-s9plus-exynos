/* Diagnostic only: compare real write-combining readback loops, with byte checks. */
#define NATIVE_METAL_SNAPSHOT_MAIN previous_snapshot_api_main
#include "native-metal-snapshot-api.m"

__attribute__((noinline)) static void system_copy(void *destination,const void *source,size_t length)
{memcpy(destination,source,length);}

__attribute__((noinline)) static void wide_copy(void *destination,const void *source,size_t length)
{
    size_t blocks=length/256;unsigned char *to=destination;const unsigned char *from=source;
    if(blocks) __asm__ volatile(
        "1:\n"
        "ldp q0,q1,[%[from],#0]\nldp q2,q3,[%[from],#32]\n"
        "ldp q4,q5,[%[from],#64]\nldp q6,q7,[%[from],#96]\n"
        "ldp q16,q17,[%[from],#128]\nldp q18,q19,[%[from],#160]\n"
        "ldp q20,q21,[%[from],#192]\nldp q22,q23,[%[from],#224]\n"
        "stp q0,q1,[%[to],#0]\nstp q2,q3,[%[to],#32]\n"
        "stp q4,q5,[%[to],#64]\nstp q6,q7,[%[to],#96]\n"
        "stp q16,q17,[%[to],#128]\nstp q18,q19,[%[to],#160]\n"
        "stp q20,q21,[%[to],#192]\nstp q22,q23,[%[to],#224]\n"
        "add %[from],%[from],#256\nadd %[to],%[to],#256\n"
        "subs %[blocks],%[blocks],#1\nb.ne 1b\n"
        : [from]"+r"(from),[to]"+r"(to),[blocks]"+r"(blocks) : :
          "v0","v1","v2","v3","v4","v5","v6","v7",
          "v16","v17","v18","v19","v20","v21","v22","v23","memory","cc");
    memcpy(to,from,length%256);
}

__attribute__((noinline)) static void group_copy(void *destination,const void *source,size_t length)
{
    size_t blocks=length/256;unsigned char *to=destination;const unsigned char *from=source;
    if(blocks) __asm__ volatile(
        "1:\n"
        "ld1 {v0.16b,v1.16b,v2.16b,v3.16b},[%[from]],#64\n"
        "ld1 {v4.16b,v5.16b,v6.16b,v7.16b},[%[from]],#64\n"
        "ld1 {v16.16b,v17.16b,v18.16b,v19.16b},[%[from]],#64\n"
        "ld1 {v20.16b,v21.16b,v22.16b,v23.16b},[%[from]],#64\n"
        "st1 {v0.16b,v1.16b,v2.16b,v3.16b},[%[to]],#64\n"
        "st1 {v4.16b,v5.16b,v6.16b,v7.16b},[%[to]],#64\n"
        "st1 {v16.16b,v17.16b,v18.16b,v19.16b},[%[to]],#64\n"
        "st1 {v20.16b,v21.16b,v22.16b,v23.16b},[%[to]],#64\n"
        "subs %[blocks],%[blocks],#1\nb.ne 1b\n"
        : [from]"+r"(from),[to]"+r"(to),[blocks]"+r"(blocks) : :
          "v0","v1","v2","v3","v4","v5","v6","v7",
          "v16","v17","v18","v19","v20","v21","v22","v23","memory","cc");
    memcpy(to,from,length%256);
}

static int wc_readback(void)
{
    @autoreleasepool {
        const size_t length=12*1024*1024;void *cached=NULL;
        if(!verify(posix_memalign(&cached,64,length)==0,"WC benchmark cached destination"))return 1;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLBuffer> gpu=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
        if(!verify(gpu && gpu.contents,"WC benchmark native mapped GPU buffer")){free(cached);return 1;}
        id<MTLCommandBuffer> commands=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
        [blit fillBuffer:gpu range:NSMakeRange(0,length) value:0x5a];[blit endEncoding];
        if(!verify(complete(commands),"WC benchmark GPU initialization")){free(cached);return 1;}
        struct Copy {const char *name;void (*copy)(void *,const void *,size_t);} variants[]={
            {"system",system_copy},{"wide256",wide_copy},{"group256",group_copy}};
        for(unsigned round=0;round<4;round++)for(unsigned variant=0;variant<3;variant++) {
            memset(cached,0,length);
            uint64_t began=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            variants[variant].copy(cached,gpu.contents,length);
            uint64_t elapsed=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-began;
            unsigned wrong=0;for(size_t i=0;i<length;i++)wrong+=((unsigned char *)cached)[i]!=0x5a;
            printf("METALWC round=%u variant=%s bytes=%lu copy_us=%llu wrong=%u\n",round,variants[variant].name,
                (unsigned long)length,(unsigned long long)(elapsed/1000),wrong);
            verify(wrong==0,"WC readback whole allocation exact");
        }
        free(cached);
    }
    printf("METALWC RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}

int main(int argc,char **argv)
{
    int result=previous_snapshot_api_main(argc,argv);
    if(argc!=2 || strcmp(argv[1],"graphics"))return result;
    alarm(20);atomic_store(&finished,0);pthread_t watcher;
    int started=pthread_create(&watcher,NULL,watch_memory,NULL)==0;
    result|=wc_readback();
    atomic_store(&finished,1);if(started)pthread_join(watcher,NULL);alarm(0);
    return result;
}
