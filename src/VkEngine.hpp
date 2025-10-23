#pragma once

#include "VkTypes.hpp"

class VulkanEngine
{
public:
    static VulkanEngine& Get();

    void init();

    void cleanup();

    void draw();

    void run();

private:
    bool       m_isInitialized = false;
    int        m_frameNumber   = 0;
    bool       m_stopRendering = false;
    VkExtent2D m_windowExtent  = {1700, 900};

    struct SDL_Window* m_window = nullptr;
};
