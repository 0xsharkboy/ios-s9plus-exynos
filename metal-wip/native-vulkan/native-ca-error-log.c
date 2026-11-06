/* Reveal QuartzCore's existing fatal log, then forward it unchanged to os_log.
 * Loaded only into the diagnostic compositor process. No shader/API behavior changes.
 */
#include <dlfcn.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <os/log.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

static _Thread_local int busy;
static unsigned reported;
_Static_assert(sizeof(vm_address_t)==sizeof(uintptr_t),"64-bit task addresses required");

static void print_string(uintptr_t address)
{
    char data[65];
    if(!address) {fputs("(null)",stderr);return;}
    for(unsigned offset=0;offset<512;offset+=64) {
        vm_size_t copied=0;
        if(vm_read_overwrite(mach_task_self(),address+offset,64,(vm_address_t)(uintptr_t)data,&copied)!=KERN_SUCCESS || copied!=64) {
            fputs("<unreadable>",stderr);return;
        }
        data[64]=0;
        size_t count=strnlen(data,64);
        fwrite(data,1,count,stderr);
        if(count<64)return;
    }
    fputs("...",stderr);
}

static void log_ca_error(void *dso,os_log_t log,os_log_type_t type,const char *format,uint8_t *buffer,uint32_t size)
{
    int saved=errno;
    if(!busy && type==OS_LOG_TYPE_ERROR && format && !strcmp(format,"%s: %s\n")) {
        busy=1;
        Dl_info info={0};
        if(dladdr(dso,&info) && info.dli_fname && strstr(info.dli_fname,"QuartzCore.framework/QuartzCore") &&
            __atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)<32) {
            fputs("METAL_CA_ERROR ",stderr);
            if(size>=2) {
                unsigned at=2;
                for(unsigned i=0;i<buffer[1] && at+2<=size;i++) {
                    unsigned tag=buffer[at],length=buffer[at+1];at+=2;
                    if(length>size-at)break;
                    if((tag>>4)==2 && length==sizeof(uintptr_t)) {
                        uintptr_t pointer;memcpy(&pointer,buffer+at,sizeof pointer);
                        print_string(pointer);fputc(' ',stderr);
                    }
                    at+=length;
                }
            }
            fputc('\n',stderr);fflush(stderr);
        }
        busy=0;
    }
    errno=saved;
    _os_log_error_impl(dso,log,type,format,buffer,size);
}

static bool enable_ca_errors(os_log_t log,os_log_type_t type)
{
    int saved=errno;Dl_info info={0};
    if(!busy && type==OS_LOG_TYPE_ERROR) {
        busy=1;
        int from_ca=dladdr(__builtin_return_address(0),&info) && info.dli_fname &&
            strstr(info.dli_fname,"QuartzCore.framework/QuartzCore");
        busy=0;errno=saved;
        if(from_ca)return true;
    }
    errno=saved;
    return os_log_type_enabled(log,type);
}

static void capture_fragment_error(const char *function,const char *spec)
{
    fputs("METAL_CA_FATAL function=",stderr);print_string((uintptr_t)function);
    fputs(" spec=",stderr);print_string((uintptr_t)spec);fputc('\n',stderr);fflush(stderr);
}

extern const void *_dyld_get_shared_cache_range(size_t *length);
static uintptr_t diagnostic_cache_slide;
static struct sigaction previous_segv,previous_bus;
static volatile sig_atomic_t fault_reporting;
static void print_region(const char *label,uintptr_t pointer)
{
    vm_address_t address=pointer;vm_size_t size=0;
    vm_region_basic_info_data_64_t info={0};mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t status=vm_region_64(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    fprintf(stderr,"NATIVE_FAULT_REGION label=%s pointer=%#lx status=%d start=%#llx bytes=%#llx protection=%x max=%x contains=%d\n",
        label,(unsigned long)pointer,status,(unsigned long long)address,(unsigned long long)size,
        info.protection,info.max_protection,status==KERN_SUCCESS && address<=pointer && pointer-address<size);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
}
static void report_native_fault(int number,siginfo_t *info,void *context)
{
    if(!fault_reporting) {
        fault_reporting=1;
        ucontext_t *uc=context;
        if(uc && uc->uc_mcontext) {
            const uintptr_t *registers=(const uintptr_t *)uc->uc_mcontext->__ss.__x;
            fprintf(stderr,"NATIVE_FAULT signal=%d address=%p pc=%#llx lr=%#llx x0=%#lx x1=%#lx x2=%#lx\n",
                number,info?info->si_addr:NULL,uc->uc_mcontext->__ss.__pc,uc->uc_mcontext->__ss.__lr,
                (unsigned long)registers[0],(unsigned long)registers[1],(unsigned long)registers[2]);
            print_region("x0",registers[0]);print_region("x1",registers[1]);
            if(info)print_region("fault",(uintptr_t)info->si_addr);
        }
        fflush(stderr);
    }
    const struct sigaction *previous=number==SIGBUS?&previous_bus:&previous_segv;
    if((previous->sa_flags&SA_SIGINFO) && previous->sa_sigaction)previous->sa_sigaction(number,info,context);
    else {signal(number,SIG_DFL);raise(number);}
}
static void native_image_loaded(const struct mach_header *header,intptr_t loader_slide)
{
    Dl_info info={0};
    if(dladdr(header,&info) && info.dli_fname &&
        (strstr(info.dli_fname,"libhakios.vk.dylib") || strstr(info.dli_fname,"libmetal2vulkan.dylib") ||
         strstr(info.dli_fname,"HakiosMetal.bundle/HakiosMetal"))) {
        fprintf(stderr,"NATIVE_IMAGE name=%s header=%p loader_slide=%#llx normalized_base=%#lx\n",
            info.dli_fname,(const void *)header,(unsigned long long)loader_slide,
            (unsigned long)((uintptr_t)header-diagnostic_cache_slide));
    }
}
/* Default-off, bounded snapshots of the frontend's existing counters. The NOLOAD
 * lookup neither creates a device nor loads a rendering backend. */
static void *profile_native_metal(void *unused)
{
    (void)unused;
    void *handle=NULL;
    size_t (*status)(char *,size_t)=NULL;
    size_t (*telemetry)(char *,size_t)=NULL;
    void (*copies)(uint64_t *)=NULL;
    void *runtime=NULL;
    for(unsigned tick=0;tick<120;tick++) {
        sleep(1);
        if(!handle)handle=dlopen("/redwood/lib/HakiosMetal.bundle/HakiosMetal",RTLD_LAZY|RTLD_NOLOAD);
        if(handle && !status) {
            status=(size_t (*)(char *,size_t))dlsym(handle,"native_bridge_metal_status");
            telemetry=(size_t (*)(char *,size_t))dlsym(handle,"native_bridge_metal_telemetry");
        }
        if(!runtime)runtime=dlopen("/redwood/lib/libmetal2vulkan.dylib",RTLD_LAZY|RTLD_NOLOAD);
        if(runtime && !copies)copies=(void (*)(uint64_t *))dlsym(runtime,"nmb_host_copies");
        char line[8192]={0};
        if(status) {
            status(line,sizeof line);line[sizeof line-1]=0;
            fprintf(stderr,"NATIVE_METAL_STATUS tick=%u %s\n",tick,line);
        }
        if(telemetry && tick%5==0) {
            telemetry(line,sizeof line);line[sizeof line-1]=0;
            fprintf(stderr,"NATIVE_METAL_TELEMETRY tick=%u\n%s\n",tick,line);
        }
        if(copies && tick%5==0) {
            uint64_t counts[4]={0};copies(counts);
            fprintf(stderr,"NATIVE_METAL_HOST_COPIES tick=%u input_bytes=%llu output_bytes=%llu input_ns=%llu output_ns=%llu\n",
                tick,(unsigned long long)counts[0],(unsigned long long)counts[1],
                (unsigned long long)counts[2],(unsigned long long)counts[3]);
        }
    }
    if(handle)dlclose(handle);
    if(runtime)dlclose(runtime);
    return NULL;
}
__attribute__((constructor)) static void loaded(void)
{
    fputs("METAL_CA_LOG_READY\n",stderr);
    const char *profile=getenv("SCION_NATIVE_METAL_PROFILE");
    if(profile && !strcmp(profile,"1")) {
        pthread_t thread;
        if(!pthread_create(&thread,NULL,profile_native_metal,NULL))pthread_detach(thread);
    }
    size_t length=0;uintptr_t cache=(uintptr_t)_dyld_get_shared_cache_range(&length);
    uintptr_t slide=cache-0x180000000ull;
    if(cache) {
        diagnostic_cache_slide=slide;
        _dyld_register_func_for_add_image(native_image_loaded);
    }
    struct sigaction diagnostic={0};diagnostic.sa_sigaction=report_native_fault;diagnostic.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&diagnostic,&previous_segv);sigaction(SIGBUS,&diagnostic,&previous_bus);
    void *metal=dlsym(RTLD_DEFAULT,"MTLCreateSystemDefaultDevice");
    if(!cache || (uintptr_t)metal-slide!=0x181a34b3cull || length<0x847d430) {
        fputs("METAL_CA_CAPTURE unsupported cache; left unchanged\n",stderr);return;
    }
    uint32_t *branch=(void *)(slide+0x18847d3e4ull);
    uint32_t *block=(void *)(slide+0x18847d410ull);
    const uint32_t expected[]={0xd0000903,0x9109d063,0x9100c3e4,0xaa1303e1,0x52800202,0x528002c5,0x955ced97};
    if(*branch!=0x34000240 || memcmp(block,expected,sizeof expected)) {
        fputs("METAL_CA_CAPTURE instruction mismatch; left unchanged\n",stderr);return;
    }
    uintptr_t page=(uintptr_t)branch&~((uintptr_t)vm_page_size-1);
    if(vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) {
        fputs("METAL_CA_CAPTURE code COW denied; left unchanged\n",stderr);return;
    }
    /* Same fatal path, same eventual abort. Replace only its log call with
     * a capture of x24 (function string) and sp+0x50 (formatted shader spec). */
    uint32_t replacement[]={0xaa1803e0,0x910143e1,0x58000070,0xd63f0200,0x14000003,0,0};
    uintptr_t target=(uintptr_t)capture_fragment_error;
    memcpy(replacement+5,&target,sizeof target);
    memcpy(block,replacement,sizeof replacement);*branch=0xd503201f;
    int coherent=sys_cache_control(kCacheFunctionPrepareForExecution,(void *)page,vm_page_size);
    if(coherent) {
        memcpy(block,expected,sizeof expected);*branch=0x34000240;
        (void)sys_cache_control(kCacheFunctionPrepareForExecution,(void *)page,vm_page_size);
    }
    kern_return_t protect=vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
    fprintf(stderr,"METAL_CA_CAPTURE patched=%d protection=0x%x\n",!coherent,protect);
}

__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} interpose[]={
    {(const void *)log_ca_error,(const void *)_os_log_error_impl},
    {(const void *)enable_ca_errors,(const void *)os_log_type_enabled}
};
