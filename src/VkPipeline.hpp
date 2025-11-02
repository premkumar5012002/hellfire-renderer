#pragma once

#include <vector>

#include "VkTypes.hpp"
#include "VkContext.hpp"

namespace VkUtils {
    bool loadShaderModule(
        const char* filePath,
        VkDevice device,
        VkShaderModule* outShaderModule
    );
}

class PipelineBuilder {
public:
    PipelineBuilder(VulkanContext* ctx) : m_ctx(ctx) {
        clear();
    }

    void clear();

    void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);

    void setInputTopology(VkPrimitiveTopology topology);

    void setPolygonMode(VkPolygonMode mode);

    void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);

    void setMultiSamplingNone();

    void disableBlending();

    void setColorAttachmentFormat(VkFormat format);

    void setDepthFormat(VkFormat format);

    void disableDepthTest();

    VkPipeline buildPipeline(VkDevice device);

    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;

    VkPipelineInputAssemblyStateCreateInfo m_inputAssembly;
    VkPipelineRasterizationStateCreateInfo m_rasterizer;
    VkPipelineColorBlendAttachmentState m_colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo m_multisampling;
    VkPipelineLayout m_pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo m_depthStencil;
    VkPipelineRenderingCreateInfo m_renderInfo;
    VkFormat m_colorAttachmentFormat;

private:
    VulkanContext* m_ctx;
};
