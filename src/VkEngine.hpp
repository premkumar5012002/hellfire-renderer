#pragma once

#include <memory>

#include "VkTypes.hpp"
#include "VkContext.hpp"
#include "VkSwapChain.hpp"

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapChainSemaphore, renderSemaphore;
    VkFence renderFence;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:
    static VulkanEngine &Get();

    [[nodiscard]] struct SDL_Window *getWindow() const { return m_window; }
    [[nodiscard]] FrameData &getCurrentFrame() { return m_frames[m_frameNumber % FRAME_OVERLAP]; }

    void init();

    void cleanup();

    void draw();

    void run();

private:
    void initCommands();

    void initSyncStructures();

    int m_frameNumber = 0;
    bool m_isInitialized = false;
    bool m_stopRendering = false;
    FrameData m_frames[FRAME_OVERLAP] = {};
    VkExtent2D m_windowExtent = {1700, 900};

    SDL_Window *m_window = nullptr;
    std::unique_ptr<VulkanContext> m_ctx = nullptr;
    std::unique_ptr<VulkanSwapChain> m_swapChain = nullptr;
};
