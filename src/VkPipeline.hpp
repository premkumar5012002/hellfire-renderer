#pragma once

#include <vulkan/vulkan.h>

namespace VkUtils {
    bool loadShaderModule(
        const char *filePath,
        VkDevice device,
        VkShaderModule *outShaderModule
    );
}
