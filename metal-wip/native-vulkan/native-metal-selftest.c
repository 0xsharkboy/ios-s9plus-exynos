/* Run the existing Metal-level bridge tests against the real native ICD.
 * The bridge embeds the upstream's owned runtime_examples AIR fixture.
 */
#include "NativeMetalBridge.h"
#include <dlfcn.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

static atomic_bool stop_monitor;
static atomic_uint_fast64_t peak_resident;
static atomic_uint stage;
static void mark(uint32_t value)
{
    atomic_store(&stage,value);
    printf("M2VTEST stage=0x%04x\n",value);
}
static void crash(int number, siginfo_t *info, void *context)
{
    ucontext_t *state=context;
    uintptr_t pc=state->uc_mcontext->__ss.__pc;
    Dl_info image={0}; dladdr((void *)pc,&image);
    char line[768];
    int size=snprintf(line,sizeof(line),"M2VTEST CRASH signal=%d code=%d stage=0x%04x pc=0x%llx base=0x%llx offset=0x%llx image=%s\n",
        number,info->si_code,atomic_load(&stage),(unsigned long long)pc,(unsigned long long)(uintptr_t)image.dli_fbase,
        (unsigned long long)(pc-(uintptr_t)image.dli_fbase),image.dli_fname?image.dli_fname:"unknown");
    if(size>0)write(STDERR_FILENO,line,(size_t)size<sizeof(line)?(size_t)size:sizeof(line)-1);
    _exit(number==SIGALRM?124:128+number);
}
static void *monitor(void *unused)
{
    (void)unused;
    while(!atomic_load(&stop_monitor)) {
        struct mach_task_basic_info info;
        mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
        kern_return_t error=task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&count);
        if(error!=KERN_SUCCESS) { fprintf(stderr,"M2VTEST cannot enforce RSS ceiling: task_info=%d\n",error);_exit(125); }
        uint64_t old=atomic_load(&peak_resident);
        if(info.resident_size>old)atomic_store(&peak_resident,info.resident_size);
        if(info.resident_size>500ull*1024*1024) { fprintf(stderr,"M2VTEST RSS ceiling exceeded at stage0x%x\n",atomic_load(&stage));_exit(137); }
        struct timespec interval={.tv_nsec=20000000};nanosleep(&interval,NULL);
    }
    return NULL;
}
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    struct sigaction action={0};action.sa_sigaction=crash;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
    sigaction(SIGILL,&action,NULL);sigaction(SIGSEGV,&action,NULL);sigaction(SIGBUS,&action,NULL);
    sigaction(SIGABRT,&action,NULL);sigaction(SIGALRM,&action,NULL);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);
    setenv("MESA_LOG","file",1);setenv("MESA_LOG_LEVEL","debug",1);setenv("PANVK_DEBUG","startup",1);
    alarm(20);
    pthread_t thread;
    if(pthread_create(&thread,NULL,monitor,NULL)) { fprintf(stderr,"M2VTEST could not start bounded RSS monitor\n");return 125; }
    void *library=dlopen("/redwood/lib/libmetal2vulkan.dylib",RTLD_NOW|RTLD_LOCAL);
    if(!library) { fprintf(stderr,"M2VTEST runtime dlopen: %s\n",dlerror());return 1; }
    typedef uint32_t (*selftest_fn)(const char *,const char *,const uint8_t *,size_t,void(*)(uint32_t),struct NmbSelftest *);
    selftest_fn run=(selftest_fn)dlsym(library,"nmb_selftest");
    if(!run) { fprintf(stderr,"M2VTEST missing nmb_selftest\n");return 1; }
    const char *cache="/private/var/tmp/native-metal-cache";
    if(mkdir(cache,0700) && access(cache,W_OK)) { fprintf(stderr,"M2VTEST no writable RAM cache\n");return 1; }
    struct NmbSelftest result={0};
    uint32_t error=run("/redwood/lib/libhakios.vk.dylib",cache,NULL,0,mark,&result);
    result.message[sizeof(result.message)-1]=0;
    alarm(0);atomic_store(&stop_monitor,true);pthread_join(thread,NULL);
    printf("M2VTEST result=0x%04x stage=0x%04x compute_bad=%u render_bad=%u import_bad=%u peak_rss=%llu message=%s\n",
        error,result.stage,result.compute_bad,result.render_bad,result.import_bad,
        (unsigned long long)atomic_load(&peak_resident),result.message);
    return error||result.compute_bad||result.render_bad||result.import_bad?1:0;
}
