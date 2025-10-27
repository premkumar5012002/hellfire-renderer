#include "VkEngine.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <chrono>
#include <thread>
#include <iostream>
#include <cassert>

#include "VkImage.hpp"

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

    // Vulkan init
    m_ctx = std::make_unique<VulkanContext>();
    m_ctx->init();

    m_swapChain = std::make_unique<VulkanSwapChain>(m_ctx.get());
    m_swapChain->init();

    initCommands();
    initSyncStructures();

    // everything went fine
    m_isInitialized = true;
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

    const VkCommandBuffer cmd = getCurrentFrame().commandBuffer;

    // now that we are sure that the commands finished executing, we can safely
    // reset the command buffer to begin recording again.
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

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

    //make the swapChain image into writeable mode before rendering
    VkUtils::transitionImage(
        cmd,
        m_swapChain->getImages()[swapChainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL
    );

    // make a clear-color from frame number. This will flash with a 120 frame period.
    VkClearColorValue clearValue;
    const float flash = std::abs(std::sin(m_frameNumber / 120.f));
    clearValue = {{0.0f, 0.0f, flash, 1.0f}};

    constexpr VkImageSubresourceRange clearRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    // clear image
    vkCmdClearColorImage(
        cmd,
        m_swapChain->getImages()[swapChainImageIndex],
        VK_IMAGE_LAYOUT_GENERAL,
        &clearValue,
        1,
        &clearRange
    );

    // make the swapChain image into presentable mode
    VkUtils::transitionImage(
        cmd,
        m_swapChain->getImages()[swapChainImageIndex],
        VK_IMAGE_LAYOUT_GENERAL,
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

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        vkDeviceWaitIdle(m_ctx->getDevice());

        for (const auto frame: m_frames) {
            vkDestroyCommandPool(m_ctx->getDevice(), frame.commandPool, nullptr);

            //destroy sync objects
            vkDestroyFence(m_ctx->getDevice(), frame.renderFence, nullptr);
            vkDestroySemaphore(m_ctx->getDevice(), frame.renderSemaphore, nullptr);
            vkDestroySemaphore(m_ctx->getDevice(), frame.swapChainSemaphore, nullptr);
        }

        m_swapChain->cleanup();

        m_ctx->cleanup();

        SDL_DestroyWindow(m_window);
        SDL_Quit();

        s_engine = nullptr;
    }
}
