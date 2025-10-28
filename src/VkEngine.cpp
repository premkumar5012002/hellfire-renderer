#include "VkEngine.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <chrono>
#include <thread>
#include <iostream>
#include <cassert>

#include "VkImage.hpp"
#include "VkPipeline.hpp"

VulkanEngine *s_engine = nullptr;

VulkanEngine &VulkanEngine::Get() {
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

    m_window = SDL_CreateWindow(
        "Vulkan Renderer",
        static_cast<int>(m_windowExtent.width), static_cast<int>(m_windowExtent.height),
        windowFlags
    );
    if (!m_window) {
        std::cerr << std::format("Failed to create SDL Window");
    }

    initVulkan();

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

    for (auto &frame: m_frames) {
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
}

void VulkanEngine::initSyncStructures() {
    // create synchronization structures
    // one fence to control when the gpu has finished rendering the frame,
    // and 2 semaphores to synchronize rendering with swapChain
    // we want the fence to start signalled so we can wait on it on the first frame
    constexpr VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    constexpr VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
    };

    for (auto &frame: m_frames) {
        VK_CHECK(vkCreateFence(m_ctx->getDevice(), &fenceInfo, nullptr, &frame.renderFence));
        VK_CHECK(vkCreateSemaphore(m_ctx->getDevice(), &semaphoreInfo, nullptr, &frame.renderSemaphore));
        VK_CHECK(vkCreateSemaphore(m_ctx->getDevice(), &semaphoreInfo, nullptr, &frame.swapChainSemaphore));
    }
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
}

void VulkanEngine::initBackgroundPipelines() {
    const VkPipelineLayoutCreateInfo computeLayout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .setLayoutCount = 1,
        .pSetLayouts = &m_drawImageDescriptorLayout,
    };

    VK_CHECK(vkCreatePipelineLayout(m_ctx->getDevice(), &computeLayout, nullptr, &m_gradientPipelineLayout));

    // layout code
    VkShaderModule computeDrawShader;
    if (!VkUtils::loadShaderModule("../resources/Shaders/gradient.comp.spv", m_ctx->getDevice(), &computeDrawShader)) {
        std::cerr << "Error when building the compute shader" << std::endl;
    }

    const VkPipelineShaderStageCreateInfo stageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = computeDrawShader,
        .pName = "main",
    };

    const VkComputePipelineCreateInfo computePipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stageInfo,
        .layout = m_gradientPipelineLayout,
    };

    VK_CHECK(
        vkCreateComputePipelines(
            m_ctx->getDevice(),
            VK_NULL_HANDLE,
            1,
            &computePipelineCreateInfo,
            nullptr,
            &m_gradientPipeline
        )
    );

    vkDestroyShaderModule(m_ctx->getDevice(), computeDrawShader, nullptr);

    m_mainDeletionQueue.push_function([&] {
        vkDestroyPipelineLayout(m_ctx->getDevice(), m_gradientPipelineLayout, nullptr);
        vkDestroyPipeline(m_ctx->getDevice(), m_gradientPipeline, nullptr);
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
        }

        // do not draw if we are minimized
        if (m_stopRendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

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
    // we will overwrite it all so we dont care about what was the older layout
    VkUtils::transitionImage(
        cmd,
        m_drawImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL
    );

    drawBackground(cmd);

    // transition the draw image and the swapChain image into their correct transfer layouts
    VkUtils::transitionImage(
        cmd,
        m_drawImage.image,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    VkUtils::transitionImage(
        cmd,
        m_swapChain->getImages()[swapChainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    // execute a copy from the draw image into the swapChain
    VkUtils::copyImageToImage(
        cmd,
        m_drawImage.image,
        m_swapChain->getImages()[swapChainImageIndex],
        m_drawExtent,
        m_swapChain->getExtent()
    );

    // set swapChain image layout to Present so we can show it on the screen
    VkUtils::transitionImage(
        cmd,
        m_swapChain->getImages()[swapChainImageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

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
    // bind the gradient drawing compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_gradientPipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_gradientPipelineLayout,
        0,
        1,
        &m_drawImageDescriptor,
        0,
        nullptr
    );

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(cmd, std::ceil(m_drawExtent.width / 16.0), std::ceil(m_drawExtent.height / 16.0), 1);
}

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        //make sure the gpu has stopped doing its things
        vkDeviceWaitIdle(m_ctx->getDevice());

        //free per-frame structures and deletion queue
        for (auto &frame: m_frames) {
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
