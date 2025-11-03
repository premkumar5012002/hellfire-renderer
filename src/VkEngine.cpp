#include "VkEngine.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <chrono>
#include <thread>
#include <iostream>
#include <cassert>

#include "VkImage.hpp"
#include "VkPipeline.hpp"

VulkanEngine* s_engine = nullptr;

VulkanEngine& VulkanEngine::Get() {
    assert(s_engine != nullptr);
    return *s_engine;
}

void VulkanEngine::init() {
    // only one engine initialization is allowed with the application.
    assert(s_engine == nullptr);
    s_engine = this;

    // We initialize SDL and create a window with it.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << std::format("Failed to init SDL Video");
    }

    constexpr SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN;

    m_window = SDL_CreateWindow("Vulkan Renderer",
                                static_cast<int>(m_windowExtent.width), static_cast<int>(m_windowExtent.height),
                                windowFlags);
    if (!m_window) {
        std::cerr << std::format("Failed to create SDL Window");
    }

    initVulkan();

    initImGui();

    // everything went fine
    m_isInitialized = true;
}

void VulkanEngine::initVulkan() {
    // Vulkan init
    m_ctx = std::make_unique<VulkanContext>();
    m_ctx->init();

    initSwapChain();

    initCommands();

    initSyncStructures();

    initDescriptors();

    initPipeline();
}

void VulkanEngine::initSwapChain() {
    m_swapChain = std::make_unique<VulkanSwapChain>(m_ctx.get());
    m_swapChain->init();

    // initialize the memory allocator
    const VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = m_ctx->getPhysicalDevice(),
        .device = m_ctx->getDevice(),
        .instance = m_ctx->getInstance(),
    };
    vmaCreateAllocator(&allocatorInfo, &m_allocator);

    m_mainDeletionQueue.push_function([&] {
        vmaDestroyAllocator(m_allocator);
    });

    // draw image size will match the window
    const VkExtent3D drawImageExtent = {
        m_windowExtent.width,
        m_windowExtent.height,
        1
    };

    //hardcoding the draw format to 32Bit float
    m_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const VkImageCreateInfo drawImageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_drawImage.imageFormat,
        .extent = drawImageExtent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = drawImageUsages,
    };

    //for the draw image, we want to allocate it from gpu local memory
    constexpr VmaAllocationCreateInfo drawImageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    // allocate and create the image
    vmaCreateImage(
        m_allocator,
        &drawImageInfo,
        &drawImageAllocInfo,
        &m_drawImage.image,
        &m_drawImage.allocation,
        nullptr
    );

    //build image-view for the draw image to use for rendering
    const VkImageViewCreateInfo drawImageView{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = m_drawImage.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_drawImage.imageFormat,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };

    VK_CHECK(vkCreateImageView(m_ctx->getDevice(), &drawImageView, nullptr, &m_drawImage.imageView));

    //add to deletion queues
    m_mainDeletionQueue.push_function([&] {
        vkDestroyImageView(m_ctx->getDevice(), m_drawImage.imageView, nullptr);
        vmaDestroyImage(m_allocator, m_drawImage.image, m_drawImage.allocation);
    });
}

void VulkanEngine::initCommands() {
    //create a command pool for commands submitted to the graphics queue.
    //we also want the pool to allow for resetting of individual command buffers
    const VkCommandPoolCreateInfo commandPoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_ctx->getQueueFamilies().graphicsFamily.value(),
    };

    for (auto& frame: m_frames) {
        VK_CHECK(vkCreateCommandPool(m_ctx->getDevice(), &commandPoolInfo, nullptr, &frame.commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VK_CHECK(vkAllocateCommandBuffers(m_ctx->getDevice(), &cmdAllocInfo, &frame.commandBuffer));
    }

    // Immediate Command Buffer
    VK_CHECK(vkCreateCommandPool(m_ctx->getDevice(), &commandPoolInfo, nullptr, &m_immediateCommandPool));

    // allocate the command buffer for immediate submits
    const VkCommandBufferAllocateInfo immediateCmdAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_immediateCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(m_ctx->getDevice(), &immediateCmdAllocInfo, &m_immediateCommandBuffer));

    m_mainDeletionQueue.push_function([&] {
        vkDestroyCommandPool(m_ctx->getDevice(), m_immediateCommandPool, nullptr);
    });
}

void VulkanEngine::initSyncStructures() {
    constexpr VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    constexpr VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
    };

    for (auto& frame: m_frames) {
        VK_CHECK(vkCreateFence(m_ctx->getDevice(), &fenceInfo, nullptr, &frame.renderFence));
        VK_CHECK(vkCreateSemaphore(m_ctx->getDevice(), &semaphoreInfo, nullptr, &frame.renderSemaphore));
        VK_CHECK(vkCreateSemaphore(m_ctx->getDevice(), &semaphoreInfo, nullptr, &frame.swapChainSemaphore));
    }

    VK_CHECK(vkCreateFence(m_ctx->getDevice(), &fenceInfo, nullptr, &m_immediateFence));

    m_mainDeletionQueue.push_function([&] {
        vkDestroyFence(m_ctx->getDevice(), m_immediateFence, nullptr);
    });
}

void VulkanEngine::initImGui() {
    //  1: create descriptor pool for IMGUI
    //  the size of the pool is very oversize, but it's copied from imgui demo
    //  itself.
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    VK_CHECK(vkCreateDescriptorPool(m_ctx->getDevice(), &poolInfo, nullptr, &m_imguiPool));

    // 2: initialize imgui library

    // this initializes the core structures of imgui
    ImGui::CreateContext();

    // this initializes imgui for SDL
    ImGui_ImplSDL3_InitForVulkan(m_window);

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = m_ctx->getInstance();
    initInfo.PhysicalDevice = m_ctx->getPhysicalDevice();
    initInfo.Device = m_ctx->getDevice();
    initInfo.Queue = m_ctx->getGraphicsQueue();
    initInfo.DescriptorPool = m_imguiPool;
    initInfo.MinImageCount = 3;
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    auto colorFormat = m_swapChain->getImageFormat();
    //dynamic rendering parameters for imgui to use
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
    };

    ImGui_ImplVulkan_Init(&initInfo);

    // add to destroy the imgui created structures
    m_mainDeletionQueue.push_function([&] {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(m_ctx->getDevice(), m_imguiPool, nullptr);
    });
}

void VulkanEngine::initDescriptors() {
    // create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
    };

    m_globalDescriptorAllocator.initPool(m_ctx->getDevice(), 10, sizes);

    // make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        m_drawImageDescriptorLayout = builder.build(m_ctx->getDevice(), VK_SHADER_STAGE_COMPUTE_BIT);
    }

    //allocate a descriptor set for our draw image
    m_drawImageDescriptor = m_globalDescriptorAllocator.allocate(m_ctx->getDevice(), m_drawImageDescriptorLayout);

    VkDescriptorImageInfo imageInfo{
        .imageView = m_drawImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkWriteDescriptorSet drawImageWrite{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = m_drawImageDescriptor,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfo,
    };

    vkUpdateDescriptorSets(m_ctx->getDevice(), 1, &drawImageWrite, 0, nullptr);

    //make sure both the descriptor allocator and the new layout get cleaned up properly
    m_mainDeletionQueue.push_function([&] {
        m_globalDescriptorAllocator.destroyPool(m_ctx->getDevice());
        vkDestroyDescriptorSetLayout(m_ctx->getDevice(), m_drawImageDescriptorLayout, nullptr);
    });
}

void VulkanEngine::initPipeline() {
    initBackgroundPipelines();
    initTrianglePipeline();
}

void VulkanEngine::initBackgroundPipelines() {
    VkPushConstantRange pushConstant{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(ComputePushConstants),
    };

    const VkPipelineLayoutCreateInfo computeLayout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .setLayoutCount = 1,
        .pSetLayouts = &m_drawImageDescriptorLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstant,
    };

    VK_CHECK(vkCreatePipelineLayout(m_ctx->getDevice(), &computeLayout, nullptr, &m_pipelineLayout));

    VkShaderModule gradientShader;
    if (!VkUtils::loadShaderModule("../../../resources/Shaders/gradient.comp.spv", m_ctx->getDevice(), &gradientShader)) {
        std::cerr << "Error when building the compute shader" << std::endl;
    }

    VkShaderModule skyShader;
    if (!VkUtils::loadShaderModule("../../../resources/Shaders/sky.comp.spv", m_ctx->getDevice(), &skyShader)) {
        std::cerr << "Error when building the compute shader" << std::endl;
    }

    const VkPipelineShaderStageCreateInfo stageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = gradientShader,
        .pName = "main",
    };

    VkComputePipelineCreateInfo computePipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stageInfo,
        .layout = m_pipelineLayout,
    };

    ComputeEffect gradient{};
    gradient.layout = m_pipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};

    // default colors
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(vkCreateComputePipelines(
        m_ctx->getDevice(),
        VK_NULL_HANDLE,
        1,
        &computePipelineCreateInfo,
        nullptr,
        &gradient.pipeline
    ));

    //change the shader module only to create the sky shader
    computePipelineCreateInfo.stage.module = skyShader;

    ComputeEffect sky{};
    gradient.layout = m_pipelineLayout;
    gradient.name = "sky";
    gradient.data = {};
    //default sky parameters
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    VK_CHECK(vkCreateComputePipelines(
        m_ctx->getDevice(),
        VK_NULL_HANDLE,
        1,
        &computePipelineCreateInfo,
        nullptr,
        &sky.pipeline
    ));

    //add the 2 background effects into the array
    m_backgroundEffects.push_back(gradient);
    m_backgroundEffects.push_back(sky);

    vkDestroyShaderModule(m_ctx->getDevice(), gradientShader, nullptr);
    vkDestroyShaderModule(m_ctx->getDevice(), skyShader, nullptr);

    m_mainDeletionQueue.push_function([&, sky, gradient] {
        vkDestroyPipelineLayout(m_ctx->getDevice(), m_pipelineLayout, nullptr);
        vkDestroyPipeline(m_ctx->getDevice(), sky.pipeline, nullptr);
        vkDestroyPipeline(m_ctx->getDevice(), gradient.pipeline, nullptr);
    });
}

void VulkanEngine::initTrianglePipeline() {
    VkShaderModule triangleFragShader;
    if (!VkUtils::loadShaderModule("../../../resources/Shaders/coloredTriangle.frag.spv", m_ctx->getDevice(), &triangleFragShader)) {
        std::cerr << std::format("Error when building the triangle fragment shader module");
    } else {
        std::cout << std::format("Triangle fragment shader successfully loaded");
    }

    VkShaderModule triangleVertexShader;
    if (!VkUtils::loadShaderModule("../../../resources/Shaders/coloredTriangle.vert.spv", m_ctx->getDevice(), &triangleVertexShader)) {
        std::cerr << std::format("Error when building the triangle vertex shader module");
    } else {
        std::cout << std::format("Triangle vertex shader successfully loaded");
    }

    //build the pipeline layout that controls the inputs/outputs of the shader
    //we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
    constexpr VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
    };
    VK_CHECK(vkCreatePipelineLayout(m_ctx->getDevice(), &pipelineLayoutInfo, nullptr, &m_trianglePipelineLayout));

    PipelineBuilder pipelineBuilder(m_ctx.get());

    //use the triangle layout we created
    pipelineBuilder.m_pipelineLayout = m_trianglePipelineLayout;
    //connecting the vertex and pixel shaders to the pipeline
    pipelineBuilder.setShaders(triangleVertexShader, triangleFragShader);
    //it will draw triangles
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //filled triangles
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    //no backface culling
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //no multisampling
    pipelineBuilder.setMultiSamplingNone();
    //no blending
    pipelineBuilder.disableBlending();
    //no depth testing
    pipelineBuilder.disableDepthTest();

    //connect the image format we will draw into, from draw image
    pipelineBuilder.setColorAttachmentFormat(m_drawImage.imageFormat);
    pipelineBuilder.setDepthFormat(VK_FORMAT_UNDEFINED);

    //finally build the pipeline
    m_trianglePipeline = pipelineBuilder.buildPipeline(m_ctx->getDevice());

    //clean structures
    vkDestroyShaderModule(m_ctx->getDevice(), triangleFragShader, nullptr);
    vkDestroyShaderModule(m_ctx->getDevice(), triangleVertexShader, nullptr);

    m_mainDeletionQueue.push_function([&] {
        vkDestroyPipelineLayout(m_ctx->getDevice(), m_trianglePipelineLayout, nullptr);
        vkDestroyPipeline(m_ctx->getDevice(), m_trianglePipeline, nullptr);
    });
}

void VulkanEngine::run() {
    SDL_Event event;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                bQuit = true;
            }

            if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                m_stopRendering = true;
            }

            if (event.type == SDL_EVENT_WINDOW_RESTORED) {
                m_stopRendering = false;
            }

            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        // do not draw if we are minimized
        if (m_stopRendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("background")) {
            ComputeEffect& selected = m_backgroundEffects[m_currentBackgroundEffect];

            ImGui::Text("Selected effect: ", selected.name);

            ImGui::SliderInt("Effect Index", &m_currentBackgroundEffect, 0, m_backgroundEffects.size() - 1);

            ImGui::InputFloat4("data1", reinterpret_cast<float *>(&selected.data.data1));
            ImGui::InputFloat4("data2", reinterpret_cast<float *>(&selected.data.data2));
            ImGui::InputFloat4("data3", reinterpret_cast<float *>(&selected.data.data3));
            ImGui::InputFloat4("data4", reinterpret_cast<float *>(&selected.data.data4));
        }
        ImGui::End();

        //make imgui calculate internal draw structures
        ImGui::Render();

        draw();
    }
}

void VulkanEngine::draw() {
    // wait until the gpu has finished rendering the last frame. Timeout of 1 second
    VK_CHECK(vkWaitForFences(m_ctx->getDevice(), 1, &getCurrentFrame().renderFence, true, 1000000000));

    getCurrentFrame().deletionQueue.flush();

    VK_CHECK(vkResetFences(m_ctx->getDevice(), 1, &getCurrentFrame().renderFence));

    // request image from the swapChain
    uint32_t swapChainImageIndex;

    VK_CHECK(
        vkAcquireNextImageKHR(
            m_ctx->getDevice(),
            m_swapChain->getSwapChain(),
            1000000000,
            getCurrentFrame().swapChainSemaphore,
            nullptr,
            &swapChainImageIndex
        )
    );

    VkCommandBuffer cmd = getCurrentFrame().commandBuffer;

    // now that we are sure that the commands finished executing, we can safely
    // reset the command buffer to begin recording again.
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    m_drawExtent.width = m_drawImage.imageExtent.width;
    m_drawExtent.height = m_drawImage.imageExtent.height;

    // begin the command buffer recording. We will use this command buffer exactly once,
    // so we want to let vulkan know that
    constexpr VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };

    // start the command buffer recording
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // transition our main draw image into general layout so we can write into it
    // we will overwrite it all so we don't care about what was the older layout
    VkUtils::transitionImage(cmd, m_drawImage.image,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);

    drawBackground(cmd);

    VkUtils::transitionImage(cmd, m_drawImage.image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    drawGeometry(cmd);

    // transition the draw image and the swapChain image into their correct transfer layouts
    VkUtils::transitionImage(cmd, m_drawImage.image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkUtils::transitionImage(cmd, m_swapChain->getImages()[swapChainImageIndex],
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // execute a copy from the draw image into the swapChain
    VkUtils::copyImageToImage(cmd, m_drawImage.image, m_swapChain->getImages()[swapChainImageIndex], m_drawExtent, m_swapChain->getExtent());

    // set swapChain image layout to Attachment Optimal so we can draw it
    VkUtils::transitionImage(cmd, m_swapChain->getImages()[swapChainImageIndex],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    //draw imGui into the swapChain image
    drawImGui(cmd, m_swapChain->getImageViews()[swapChainImageIndex]);

    // set swapChain image layout to Present so we can show it on the screen
    VkUtils::transitionImage(cmd, m_swapChain->getImages()[swapChainImageIndex],
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd));

    // prepare the submission to the queue.
    // we want to wait on the presentSemaphore, as that semaphore is signaled when the swapChain is ready
    // we will signal the renderSemaphore, to signal that rendering has finished
    VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBuffer = cmd,
        .deviceMask = 0,
    };

    VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = getCurrentFrame().swapChainSemaphore,
        .value = 1,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        .deviceIndex = 0,
    };

    VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = getCurrentFrame().renderSemaphore,
        .value = 1,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        .deviceIndex = 0,
    };

    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo,
    };

    // submit command buffer to the queue and execute it.
    // renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(m_ctx->getGraphicsQueue(), 1, &submit, getCurrentFrame().renderFence));

    // prepare present
    // this will put the image we just rendered to into the visible window.
    // we want to wait on the renderSemaphore for that,
    // as its necessary that drawing commands have finished before the image is displayed to the user
    const VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &getCurrentFrame().renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &m_swapChain->getSwapChain(),
        .pImageIndices = &swapChainImageIndex
    };

    VK_CHECK(vkQueuePresentKHR(m_ctx->getGraphicsQueue(), &presentInfo));

    // increase the number of frames drawn
    m_frameNumber++;
}

void VulkanEngine::drawBackground(VkCommandBuffer cmd) const {
    ComputeEffect effect = m_backgroundEffects[m_currentBackgroundEffect];

    // bind the gradient drawing compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_drawImageDescriptor, 0, nullptr);

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(cmd, std::ceil(m_drawExtent.width / 16.0), std::ceil(m_drawExtent.height / 16.0), 1);
}

void VulkanEngine::drawGeometry(VkCommandBuffer cmd) {
    //begin a render pass  connected to our draw image
    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = m_drawImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .renderArea = {
            .offset = {0, 0},
            .extent = m_drawExtent, // Use the draw extent set in draw()
        },
        .layerCount = 1, // Set layerCount to 1 for single-layer rendering
        .viewMask = 0, // No multiview rendering
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };
    vkCmdBeginRendering(cmd, &renderInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline);

    //set dynamic viewport and scissor
    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = m_drawExtent.width;
    viewport.height = m_drawExtent.height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = m_drawExtent.width;
    scissor.extent.height = m_drawExtent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    //launch a draw command to draw 3 vertices
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::drawImGui(VkCommandBuffer cmd, VkImageView targetImageView) const {
    VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = targetImageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    const VkRenderingInfo renderInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .renderArea = {
            .offset = {0, 0},
            .extent = m_swapChain->getExtent(), // Use the draw extent set in draw()
        },
        .layerCount = 1, // Set layerCount to 1 for single-layer rendering
        .viewMask = 0, // No multiview rendering
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) const {
    VK_CHECK(vkResetFences(m_ctx->getDevice(), 1, &m_immediateFence));
    VK_CHECK(vkResetCommandBuffer(m_immediateCommandBuffer, 0));

    VkCommandBuffer cmd = m_immediateCommandBuffer;

    constexpr VkCommandBufferBeginInfo cmdBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBuffer = cmd,
        .deviceMask = 0,
    };

    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,
        .waitSemaphoreInfoCount = 0, // No semaphores to wait on
        .pWaitSemaphoreInfos = nullptr,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 0, // No semaphores to signal
        .pSignalSemaphoreInfos = nullptr,
    };

    // submit command buffer to the queue and execute it.
    // renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(m_ctx->getGraphicsQueue(), 1, &submitInfo, m_immediateFence));

    VK_CHECK(vkWaitForFences(m_ctx->getDevice(), 1, &m_immediateFence, true, 9999999999));
}

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        //make sure the gpu has stopped doing its things
        vkDeviceWaitIdle(m_ctx->getDevice());

        //free per-frame structures and deletion queue
        for (auto& frame: m_frames) {
            vkDestroyCommandPool(m_ctx->getDevice(), frame.commandPool, nullptr);

            //destroy sync objects
            vkDestroyFence(m_ctx->getDevice(), frame.renderFence, nullptr);
            vkDestroySemaphore(m_ctx->getDevice(), frame.renderSemaphore, nullptr);
            vkDestroySemaphore(m_ctx->getDevice(), frame.swapChainSemaphore, nullptr);

            frame.deletionQueue.flush();
        }

        //flush the global deletion queue
        m_mainDeletionQueue.flush();

        m_swapChain->cleanup();

        m_ctx->cleanup();

        SDL_DestroyWindow(m_window);
        SDL_Quit();

        s_engine = nullptr;
    }
}
