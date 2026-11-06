#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "fetch-shaders.h"

#define FUNCTIONS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
 X(GetPhysicalDeviceMemoryProperties) X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) \
 X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
 X(InvalidateMappedMemoryRanges) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
 X(CreateImageView) X(DestroyImageView) X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) \
 X(CreateDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreatePipelineLayout) X(CreateRenderPass) \
 X(CreateFramebuffer) X(CreateGraphicsPipelines) X(CreateCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) \
 X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdClearColorImage) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
 X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdSetScissor) X(CmdDraw) X(CmdCopyImageToBuffer) X(CreateFence) \
 X(QueueSubmit) X(WaitForFences) X(DeviceWaitIdle) X(DestroyFence) X(DestroyPipeline) X(DestroyFramebuffer) \
 X(DestroyRenderPass) X(DestroyPipelineLayout) X(DestroyDescriptorPool) X(DestroyDescriptorSetLayout) X(DestroyCommandPool)
#define DECLARE(name) static PFN_vk##name vk##name;
FUNCTIONS(DECLARE)
#undef DECLARE
#define CHECK(call) do {VkResult result=(call);if(result!=VK_SUCCESS){fprintf(stderr,"FETCH FAIL line=%d result=%d %s\n",__LINE__,result,#call);return 1;}}while(0)
enum {SIDE=64};
static uint32_t memory_type(VkPhysicalDeviceMemoryProperties *p,uint32_t mask,VkMemoryPropertyFlags flags)
{
    for(uint32_t i=0;i<p->memoryTypeCount;i++)if((mask&(1u<<i)) && (p->memoryTypes[i].propertyFlags&flags)==flags)return i;
    fputs("FETCH FAIL no memory type\n",stderr);exit(2);
}
static void between(VkCommandBuffer cb)
{
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_DEPENDENCY_BY_REGION_BIT,1,&barrier,0,NULL,0,NULL);
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);alarm(20);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);setenv("MESA_LOG","file",1);setenv("MESA_LOG_LEVEL","debug",1);
    void *library=dlopen("/redwood/lib/libhakios.vk.dylib",RTLD_NOW|RTLD_LOCAL);
    PFN_vkGetInstanceProcAddr get=library?(PFN_vkGetInstanceProcAddr)dlsym(library,"vk_icdGetInstanceProcAddr"):NULL;
    if(!get){printf("FETCH FAIL ICD %s\n",dlerror());return 1;}
    PFN_vkCreateInstance create=(PFN_vkCreateInstance)get(VK_NULL_HANDLE,"vkCreateInstance");
    VkApplicationInfo application={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="native-fetch-test",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&application};VkInstance instance;
    CHECK(create(&ici,NULL,&instance));
#define LOAD(name) vk##name=(PFN_vk##name)get(instance,"vk"#name);if(!vk##name){fputs("FETCH FAIL entry vk"#name"\n",stderr);return 1;}
    FUNCTIONS(LOAD)
#undef LOAD
    uint32_t count=1;VkPhysicalDevice physical;CHECK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    printf("FETCH device=%s; no ordered-access or interlock extension requested\n",properties.deviceName);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);
    VkQueueFamilyProperties *family=calloc(families,sizeof *family);vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,family);
    uint32_t queue_family=UINT32_MAX;
    for(uint32_t i=0;i<families;i++)if(family[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){queue_family=i;break;}
    free(family);if(queue_family==UINT32_MAX)return 2;
    float priority=1;VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=queue_family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};VkDevice device;
    CHECK(vkCreateDevice(physical,&dci,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,queue_family,0,&queue);
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    VkImageCreateInfo image_info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_SFLOAT,
        .extent={SIDE,SIDE,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};VkImage image;
    CHECK(vkCreateImage(device,&image_info,NULL,&image));VkMemoryRequirements requirements;vkGetImageMemoryRequirements(device,image,&requirements);
    VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=memory_type(&memory,requirements.memoryTypeBits,0)};
    VkDeviceMemory image_memory;CHECK(vkAllocateMemory(device,&allocation,NULL,&image_memory));CHECK(vkBindImageMemory(device,image,image_memory,0));
    VkImageSubresourceRange range={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1};
    VkImageViewCreateInfo view_info={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R32_SFLOAT,.subresourceRange=range};VkImageView view;
    CHECK(vkCreateImageView(device,&view_info,NULL,&view));
    VkBufferCreateInfo buffer_info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=SIDE*SIDE*sizeof(float),.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};VkBuffer buffer;
    CHECK(vkCreateBuffer(device,&buffer_info,NULL,&buffer));vkGetBufferMemoryRequirements(device,buffer,&requirements);
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=memory_type(&memory,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    VkDeviceMemory buffer_memory;CHECK(vkAllocateMemory(device,&allocation,NULL,&buffer_memory));CHECK(vkBindBufferMemory(device,buffer,buffer_memory,0));
    void *mapped;CHECK(vkMapMemory(device,buffer_memory,0,VK_WHOLE_SIZE,0,&mapped));
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout layout;
    CHECK(vkCreateDescriptorSetLayout(device,&slci,NULL,&layout));
    VkDescriptorPoolSize pool_size={.type=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.descriptorCount=1};
    VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};VkDescriptorPool pool;
    CHECK(vkCreateDescriptorPool(device,&dpci,NULL,&pool));
    VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(device,&dsai,&set));
    VkDescriptorImageInfo descriptor={.imageView=view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.pImageInfo=&descriptor};
    vkUpdateDescriptorSets(device,1,&write,0,NULL);
    VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};VkPipelineLayout pipeline_layout;
    CHECK(vkCreatePipelineLayout(device,&plci,NULL,&pipeline_layout));
    VkAttachmentDescription attachment={.format=VK_FORMAT_R32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkAttachmentReference reference={.attachment=0,.layout=VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.inputAttachmentCount=1,.pInputAttachments=&reference,.colorAttachmentCount=1,.pColorAttachments=&reference};
    VkSubpassDependency dependency={.srcSubpass=0,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT};
    VkRenderPassCreateInfo rpci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};VkRenderPass pass;
    CHECK(vkCreateRenderPass(device,&rpci,NULL,&pass));
    VkFramebufferCreateInfo fbci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&view,.width=SIDE,.height=SIDE,.layers=1};VkFramebuffer framebuffer;
    CHECK(vkCreateFramebuffer(device,&fbci,NULL,&framebuffer));
    VkShaderModuleCreateInfo smci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(fetch_vertex_spv),.pCode=fetch_vertex_spv};VkShaderModule vertex,fragment;
    CHECK(vkCreateShaderModule(device,&smci,NULL,&vertex));smci.codeSize=sizeof(fetch_fragment_spv);smci.pCode=fetch_fragment_spv;CHECK(vkCreateShaderModule(device,&smci,NULL,&fragment));
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={.width=SIDE,.height=SIDE,.maxDepth=1};VkRect2D scissor={.extent={SIDE,SIDE}};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState color={.colorWriteMask=VK_COLOR_COMPONENT_R_BIT};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&color};
    VkDynamicState dynamic_state=VK_DYNAMIC_STATE_SCISSOR;VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=1,.pDynamicStates=&dynamic_state};
    VkGraphicsPipelineCreateInfo gpci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&samples,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=pipeline_layout,.renderPass=pass};VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gpci,NULL,&pipeline));
    VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=queue_family};VkCommandPool commands;
    CHECK(vkCreateCommandPool(device,&cpci,NULL,&commands));
    VkCommandBufferAllocateInfo cbai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=2};VkCommandBuffer cb[2];
    CHECK(vkAllocateCommandBuffers(device,&cbai,cb));unsigned failed=0;
    for(unsigned mode=0;mode<2;mode++) {
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb[mode],&begin));
        VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=mode?VK_ACCESS_TRANSFER_READ_BIT:0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout=mode?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange=range};
        vkCmdPipelineBarrier(cb[mode],mode?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
        VkClearColorValue initial={.float32={1,0,0,1}};vkCmdClearColorImage(cb[mode],image,VK_IMAGE_LAYOUT_GENERAL,&initial,1,&range);
        VkMemoryBarrier initialize={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        vkCmdPipelineBarrier(cb[mode],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,1,&initialize,0,NULL,0,NULL);
        VkRenderPassBeginInfo rbegin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={.extent={SIDE,SIDE}}};
        if(mode)vkCmdBeginRenderPass(cb[mode],&rbegin,VK_SUBPASS_CONTENTS_INLINE);
        for(unsigned draw=0;draw<4;draw++) {
            if(!mode)vkCmdBeginRenderPass(cb[mode],&rbegin,VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cb[mode],VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
            vkCmdBindDescriptorSets(cb[mode],VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,1,&set,0,NULL);
            VkRect2D area={.extent={draw==1?SIDE/2:SIDE,draw==2?SIDE/2:SIDE}};
            vkCmdSetScissor(cb[mode],0,1,&area);vkCmdDraw(cb[mode],3,1,0,0);
            if(!mode)vkCmdEndRenderPass(cb[mode]);
            if(draw<3)between(cb[mode]);
        }
        if(mode)vkCmdEndRenderPass(cb[mode]);
        ib.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_GENERAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(cb[mode],VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
        VkBufferImageCopy copy={.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},.imageExtent={SIDE,SIDE,1}};
        vkCmdCopyImageToBuffer(cb[mode],image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(cb[mode],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
        CHECK(vkEndCommandBuffer(cb[mode]));
        VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fci,NULL,&fence));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb[mode]};
        uint64_t start=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
        VkMappedMemoryRange invalidate={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=buffer_memory,.size=VK_WHOLE_SIZE};CHECK(vkInvalidateMappedMemoryRanges(device,1,&invalidate));
        unsigned wrong=0;float *pixels=mapped;
        for(unsigned y=0;y<SIDE;y++)for(unsigned x=0;x<SIDE;x++) {
            float expected=.25f*(x<SIDE/2?.5f:1.f)*(y<SIDE/2?.5f:1.f);
            wrong+=pixels[y*SIDE+x]!=expected;
        }
        printf("FETCH %s mode=%s wrong=%u/4096 elapsed_us=%llu samples=%g,%g,%g,%g\n",wrong?"FAIL":"PASS",mode?"self-dependency":"split-passes",wrong,
            (unsigned long long)((clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-start)/1000),pixels[0],pixels[SIDE-1],pixels[(SIDE-1)*SIDE],pixels[SIDE*SIDE-1]);
        failed+=wrong!=0;
        vkDestroyFence(device,fence,NULL);
    }
    CHECK(vkDeviceWaitIdle(device));printf("FETCH RESULT failed_modes=%u\n",failed);alarm(0);
    vkDestroyCommandPool(device,commands,NULL);vkDestroyPipeline(device,pipeline,NULL);
    vkDestroyShaderModule(device,vertex,NULL);vkDestroyShaderModule(device,fragment,NULL);
    vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);
    vkDestroyPipelineLayout(device,pipeline_layout,NULL);vkDestroyDescriptorPool(device,pool,NULL);
    vkDestroyDescriptorSetLayout(device,layout,NULL);vkDestroyImageView(device,view,NULL);
    vkDestroyImage(device,image,NULL);vkFreeMemory(device,image_memory,NULL);
    vkUnmapMemory(device,buffer_memory);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,buffer_memory,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);return failed?1:0;
}
