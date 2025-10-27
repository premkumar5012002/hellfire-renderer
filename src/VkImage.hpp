#pragma once

#include <vulkan/vulkan.h>

namespace VkUtils {
    void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
}
