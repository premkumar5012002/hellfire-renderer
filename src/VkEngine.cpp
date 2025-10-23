#include "VkEngine.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <chrono>
#include <thread>

VulkanEngine* s_engine = nullptr;

VulkanEngine& VulkanEngine::Get()
{
    assert(s_engine != nullptr);
    return *s_engine;
}

void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(s_engine == nullptr);
    s_engine = this;

    // We initialize SDL and create a window with it.
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
    }

    SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN;

    m_window = SDL_CreateWindow("Vulkan Renderer",
                                static_cast<int>(m_windowExtent.width),
                                static_cast<int>(m_windowExtent.height),
                                windowFlags);
    if (!m_window)
    {
    }

    // everything went fine
    m_isInitialized = true;
}

void VulkanEngine::run()
{
    SDL_Event event;
    bool      bQuit = false;

    // main loop
    while (!bQuit)
    {
        // Handle events on queue
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                bQuit = true;
            }

            if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
            {
                m_stopRendering = true;
            }

            if (event.type == SDL_EVENT_WINDOW_RESTORED)
            {
                m_stopRendering = false;
            }
        }

        // do not draw if we are minimized
        if (m_stopRendering)
        {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}

void VulkanEngine::draw() {

}

void VulkanEngine::cleanup()
{
    if (m_isInitialized)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    s_engine = nullptr;
}
