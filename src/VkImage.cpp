#include "VkImage.hpp"

namespace VkUtils {
    void transitionImage(
        const VkCommandBuffer cmd,
        const VkImage image,
        const VkImageLayout currentLayout,
        const VkImageLayout newLayout
    ) {
        const VkImageAspectFlags aspectMask = newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                                                  ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                  : VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageMemoryBarrier2 imageBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = currentLayout,
            .newLayout = newLayout,
            .image = image,
            .subresourceRange = {
                .aspectMask = aspectMask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };


        const VkDependencyInfo depInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }
}
