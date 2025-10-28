#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <array>
#include <deque>
#include <format>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#define VK_CHECK(x)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        VkResult error = x;                                                                                            \
        if (error)                                                                                                     \
        {                                                                                                              \
            std::cerr << std::format("Detected Vulkan error: {}", string_VkResult(error));                             \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

struct DeletionQueue {
    std::deque<std::function<void()> > deletors;

    void push_function(std::function<void()> &&function) {
        deletors.push_back(function);
    }

    void flush() {
        // reverse iterate the deletion queue to execute all the functions
        for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
            (*it)(); //call functors
        }
        deletors.clear();
    }
};

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapChainSemaphore, renderSemaphore;
    VkFence renderFence;
    DeletionQueue deletionQueue;
};

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};
