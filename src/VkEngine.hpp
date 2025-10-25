#pragma once

#include "VkContext.hpp"
#include "VkSwapChain.hpp"

#include <memory>

class VulkanEngine {
public:
    static VulkanEngine &Get();

    [[nodiscard]] struct SDL_Window *getWindow() const { return m_window; }

    void init();

    void cleanup();

    void draw();

    void run();

private:
    bool m_isInitialized = false;
    int m_frameNumber = 0;
    bool m_stopRendering = false;
    VkExtent2D m_windowExtent = {1700, 900};

    SDL_Window *m_window = nullptr;

    std::unique_ptr<VulkanContext> m_ctx = nullptr;
    std::unique_ptr<VulkanSwapChain> m_swapChain = nullptr;
};
