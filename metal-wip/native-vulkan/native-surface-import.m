/* SPDX-License-Identifier: MIT
 * Authored Vulkan/IOSurface interop check. No translated or Apple shader code.
 * vkCmdFillBuffer and vkCmdCopyBuffer must touch the original surface pages.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#import <Foundation/Foundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <libkern/OSCacheControl.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include "hakios-native-surface-fd.h"

#define PROCEDURES(X) \
 X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceMemoryProperties) \
 X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkEnumerateDeviceExtensionProperties) \
 X(vkCreateDevice) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) \
 X(vkAllocateMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
 X(vkCreateCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandPool) \
 X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdFillBuffer) X(vkCmdCopyBuffer) \
 X(vkCreateFence) X(vkResetFences) X(vkQueueSubmit) X(vkWaitForFences) \
 X(vkDestroyFence) X(vkDestroyCommandPool) X(vkDestroyBuffer) X(vkFreeMemory) \
 X(vkDeviceWaitIdle) X(vkDestroyDevice) X(vkDestroyInstance)
#define DECLARE(name) static PFN_##name name;
PROCEDURES(DECLARE)
#undef DECLARE

static int checks,failures;
static VkDevice device;
static VkQueue queue;
static VkCommandPool pool;
static VkCommandBuffer command;
static VkFence fence;
static VkPhysicalDeviceMemoryProperties memory_properties;
static int (*surface_fd_create)(uint32_t,uint64_t,uint64_t *);
static int (*surface_fd_release)(uint64_t);
static uint64_t micros(void)
{
    static mach_timebase_info_data_t timebase;
    if(!timebase.denom)mach_timebase_info(&timebase);
    return mach_absolute_time()*timebase.numer/timebase.denom/1000;
}
static bool check(bool good,const char *name)
{
    checks++;if(!good)failures++;
    printf("SURFACEVK %s %s\n",good?"PASS":"FAIL",name);return good;
}
static bool vk_ok(VkResult result,const char *operation)
{
    if(result==VK_SUCCESS)return true;
    fprintf(stderr,"SURFACEVK Vulkan error=%d op=%s\n",result,operation);return false;
}
#define OK(call) vk_ok((call),#call)
struct buffer { VkBuffer object;VkDeviceMemory memory;void *mapped;uint64_t bytes;bool imported_fd_consumed; };

static void release_buffer(struct buffer *buffer)
{
    if(buffer->mapped)vkUnmapMemory(device,buffer->memory);
    if(buffer->object)vkDestroyBuffer(device,buffer->object,NULL);
    if(buffer->memory)vkFreeMemory(device,buffer->memory,NULL);
    *buffer=(struct buffer){0};
}
static bool make_buffer(struct buffer *buffer,uint64_t bytes,int import_fd)
{
    VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=import_fd>=0?&external:NULL,
        .size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if(!OK(vkCreateBuffer(device,&info,NULL,&buffer->object)))return false;
    VkMemoryRequirements required;vkGetBufferMemoryRequirements(device,buffer->object,&required);
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<memory_properties.memoryTypeCount;i++)if(required.memoryTypeBits&(1u<<i)) {
        VkMemoryPropertyFlags flags=memory_properties.memoryTypes[i].propertyFlags;
        if(import_fd>=0 ? !(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) :
            (flags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {type=i;break;}
    }
    if(type==UINT32_MAX || (import_fd>=0 && required.size>bytes))return false;
    VkImportMemoryFdInfoKHR imported={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,.fd=import_fd};
    VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=import_fd>=0?&imported:NULL,
        .allocationSize=import_fd>=0?bytes:required.size,.memoryTypeIndex=type};
    if(!OK(vkAllocateMemory(device,&allocation,NULL,&buffer->memory)))return false;
    buffer->imported_fd_consumed=import_fd>=0;
    if(!OK(vkBindBufferMemory(device,buffer->object,buffer->memory,0)))return false;
    if(import_fd<0 && !OK(vkMapMemory(device,buffer->memory,0,bytes,0,&buffer->mapped)))return false;
    buffer->bytes=bytes;
    printf("SURFACEVK memory imported=%d type=%u flags=0x%x bytes=%llu\n",import_fd>=0,type,
        memory_properties.memoryTypes[type].propertyFlags,(unsigned long long)bytes);
    return true;
}
static bool begin(void)
{
    VkCommandBufferBeginInfo start={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    return OK(vkResetCommandPool(device,pool,0)) && OK(vkResetFences(device,1,&fence)) && OK(vkBeginCommandBuffer(command,&start));
}
static bool finish(void)
{
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    return OK(vkEndCommandBuffer(command)) && OK(vkQueueSubmit(queue,1,&submit,fence)) &&
        OK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
}
static bool pattern(const void *bytes,uint64_t length,uint32_t value)
{
    const uint32_t *words=bytes;
    for(uint64_t i=0;i<length/4;i++)if(words[i]!=value) {
        printf("SURFACEVK mismatch word=%llu expected=%08x actual=%08x\n",(unsigned long long)i,value,words[i]);return false;
    }
    return true;
}
static bool cache(void *bytes,uint64_t length)
{
    /* On this iOS build sys_dcache_flush is only a barrier (Apple assumes
     * hardware coherence). The tested Exynos kernel instead services trapped
     * DC CIVAC instructions to PoC; use that actual maintenance in this probe.
     * Per-line traps are measured, not proposed as the final fast range API. */
    uint64_t line=0;size_t size=sizeof line;
    if(sysctlbyname("hw.cachelinesize",&line,&size,NULL,0) || line!=64 || length>UINTPTR_MAX-(uintptr_t)bytes)return false;
    uintptr_t end=(uintptr_t)bytes+length;
    for(uintptr_t at=(uintptr_t)bytes&~(uintptr_t)(line-1);at<end;at+=line)
        __asm__ volatile("dc civac, %0"::"r"(at):"memory");
    __asm__ volatile("dsb sy":::"memory");
    return true;
}
static void run_size(unsigned height)
{
    const uint64_t bytes=1024ull*4*height;
    NSDictionary *properties=@{(id)kIOSurfaceWidth:@1024,(id)kIOSurfaceHeight:@(height),
        (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@4096,
        (id)kIOSurfaceAllocSize:@(bytes),(id)kIOSurfacePixelFormat:@(0x42475241)};
    IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)properties);
    if(!check(surface && IOSurfaceGetAllocSize(surface)==bytes,"IOSurface allocation exact size")){if(surface)CFRelease(surface);return;}
    void *pixels=IOSurfaceGetBaseAddress(surface);uint64_t token=0;
    struct buffer shared={0},ordinary={0};
    int fd=surface_fd_create(IOSurfaceGetID(surface),bytes,&token);
    if(fd<0)fprintf(stderr,"SURFACEVK descriptor errno=%d %s\n",errno,strerror(errno));
    if(!check(fd>=0 && token,"real seekable native surface descriptor"))goto cleanup;
    /* The device-local imported memory is not advertised as host coherent.
     * CPU visibility of the original IOSurface uses explicit OS DMA cache ops. */
    if(!check(make_buffer(&shared,bytes,fd),"Vulkan buffer imports original IOSurface pages")){
        if(!shared.imported_fd_consumed)close(fd);goto cleanup;
    }
    fd=-1; /* Vulkan owns and closed the successfully imported descriptor. */
    if(!check(make_buffer(&ordinary,bytes,-1),"ordinary coherent buffer for control readback"))goto cleanup;
    for(unsigned round=0;round<4;round++) {
        const uint32_t gpu_value=0x11223300u+round,host_value=0xaabbcc00u+round;
        IOSurfaceLock(surface,0,NULL);memset(pixels,0,bytes);IOSurfaceUnlock(surface,0,NULL);
        bool good=cache(pixels,bytes) && begin();
        if(good){vkCmdFillBuffer(command,shared.object,0,bytes,gpu_value);good=finish();}
        uint64_t started=micros();good=good && cache(pixels,bytes);uint64_t sync_us=micros()-started;
        if(!check(good && pattern(pixels,bytes,gpu_value),"GPU writes visible in original IOSurface"))break;
        /* Reverse direction proves the imported object reads the original pages
         * again after CPU changes, without uploading the descriptor file. */
        IOSurfaceLock(surface,0,NULL);
        uint32_t *words=pixels;for(uint64_t i=0;i<bytes/4;i++)words[i]=host_value;
        IOSurfaceUnlock(surface,0,NULL); /* publish the CPU content generation */
        good=cache(pixels,bytes) && begin();
        if(good){VkBufferCopy region={.size=bytes};vkCmdCopyBuffer(command,shared.object,ordinary.object,1,&region);good=finish();}
        if(!check(good && pattern(ordinary.mapped,bytes,host_value),"GPU reads updated original IOSurface pages"))break;
        void *scratch=malloc(bytes);if(!check(scratch!=NULL,"bounded readback scratch allocation"))break;
        for(unsigned kind=0;kind<2;kind++) {
            const void *source=kind?ordinary.mapped:pixels;
            started=micros();memcpy(scratch,source,bytes);uint64_t elapsed=micros()-started;
            bool exact=pattern(scratch,bytes,host_value);check(exact,"CPU copy byte exact");
            printf("SURFACEVK COPY bytes=%llu round=%u source=%s us=%llu invalidate_us=%llu\n",(unsigned long long)bytes,round,
                kind?"ordinary-wc":"imported-cached",(unsigned long long)elapsed,(unsigned long long)(kind?0:sync_us));
        }
        free(scratch);
    }
cleanup:
    if(device)vkDeviceWaitIdle(device);
    release_buffer(&ordinary);release_buffer(&shared);
    if(token)check(surface_fd_release(token)==0,"surface descriptor identity released");
    CFRelease(surface);
}
int main(void)
{
    @autoreleasepool {
    setvbuf(stdout,NULL,_IONBF,0);setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);
    puts("SURFACEVK cache path=explicit DC CIVAC via Exynos kernel cache-op trap");
    void *icd=dlopen("/private/var/scion-dev/live/libhakios.surface.dylib",RTLD_NOW|RTLD_LOCAL);
    if(!check(icd!=NULL,"native ICD opens")){fprintf(stderr,"%s\n",dlerror());return 1;}
    PFN_vkGetInstanceProcAddr get_proc=dlsym(icd,"vk_icdGetInstanceProcAddr");
    surface_fd_create=dlsym(icd,"hakios_native_surface_fd_create");surface_fd_release=dlsym(icd,"hakios_native_surface_fd_release");
    if(!check(get_proc && surface_fd_create && surface_fd_release,"native import entry points present"))return 1;
    PFN_vkCreateInstance create=(PFN_vkCreateInstance)get_proc(NULL,"vkCreateInstance");
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="scion-surface-import",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance=VK_NULL_HANDLE;
    if(!check(create && OK(create(&instance_info,NULL,&instance)),"Vulkan instance"))return 1;
#define LOAD(name) name=(PFN_##name)get_proc(instance,#name);if(!name){printf("SURFACEVK missing %s\n",#name);return 1;}
    PROCEDURES(LOAD)
#undef LOAD
    uint32_t count=1;VkPhysicalDevice physical=VK_NULL_HANDLE;
    if(!check(OK(vkEnumeratePhysicalDevices(instance,&count,&physical)) && count,"physical device"))return 1;
    vkGetPhysicalDeviceMemoryProperties(physical,&memory_properties);
    VkQueueFamilyProperties families[16];count=16;vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    uint32_t family=UINT32_MAX;for(uint32_t i=0;i<count && i<16;i++)if(families[i].queueFlags&VK_QUEUE_TRANSFER_BIT){family=i;break;}
    if(!check(family!=UINT32_MAX,"transfer queue family"))return 1;
    float priority=1;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extensions[]={VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME};
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,
        .enabledExtensionCount=1,.ppEnabledExtensionNames=extensions};
    if(!check(OK(vkCreateDevice(physical,&device_info,NULL,&device)),"Vulkan device with external FD import"))return 1;
    vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandBufferAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=VK_NULL_HANDLE,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(!OK(vkCreateCommandPool(device,&pool_info,NULL,&pool)))return 1;allocation.commandPool=pool;
    if(!OK(vkAllocateCommandBuffers(device,&allocation,&command)) || !OK(vkCreateFence(device,&fence_info,NULL,&fence)))return 1;
    run_size(4);run_size(3072);
    vkDeviceWaitIdle(device);vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    printf("SURFACEVK RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
    }
}
