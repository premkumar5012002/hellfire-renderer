#pragma once

#include <memory>

#include "VkTypes.hpp"
#include "VkContext.hpp"
#include "VkDescriptors.hpp"
#include "VkSwapChain.hpp"

constexpr unsigned int FRAME_OVERLAP = 2;

struct ComputeEffect {
    const char* name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    ComputePushConstants data;
};

class VulkanEngine {
public:
    static VulkanEngine& Get();

    [[nodiscard]] struct SDL_Window* getWindow() const { return m_window; }
    [[nodiscard]] FrameData& getCurrentFrame() { return m_frames[m_frameNumber % FRAME_OVERLAP]; }

    void init();
    void cleanup();
    void run();
    void draw();

private:
    void initVulkan();
    void initSwapChain();
    void initCommands();
    void initSyncStructures();
    void initDescriptors();
    void initPipeline();
    void initBackgroundPipelines();
    void initTrianglePipeline();
    void initImGui();

    void drawBackground(VkCommandBuffer cmd) const;
    void drawGeometry(VkCommandBuffer cmd);
    void drawImGui(VkCommandBuffer cmd, VkImageView targetImageView) const;

    void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) const;

    int m_frameNumber = 0;
    bool m_isInitialized = false;
    bool m_stopRendering = false;
    FrameData m_frames[FRAME_OVERLAP]{};
    VkExtent2D m_windowExtent = {1700, 900};

    DeletionQueue m_mainDeletionQueue;
    VmaAllocator m_allocator;

    AllocatedImage m_drawImage;
    VkExtent2D m_drawExtent;

    DescriptorAllocator m_globalDescriptorAllocator;

    VkDescriptorSet m_drawImageDescriptor;
    VkDescriptorSetLayout m_drawImageDescriptorLayout;

    VkPipelineLayout m_pipelineLayout;

    VkFence m_immediateFence;
    VkCommandBuffer m_immediateCommandBuffer;
    VkCommandPool m_immediateCommandPool;

    VkDescriptorPool m_imguiPool;

    int m_currentBackgroundEffect{0};
    std::vector<ComputeEffect> m_backgroundEffects;

    VkPipelineLayout m_trianglePipelineLayout;
    VkPipeline m_trianglePipeline;

    SDL_Window* m_window = nullptr;
    std::unique_ptr<VulkanContext> m_ctx = nullptr;
    std::unique_ptr<VulkanSwapChain> m_swapChain = nullptr;
};
