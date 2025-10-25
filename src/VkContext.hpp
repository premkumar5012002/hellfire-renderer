#pragma once

#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool isSameFamily() const {
        return graphicsFamily.value() == presentFamily.value();
    }

    [[nodiscard]] bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanContext {
public:
    void init();

    void cleanup();

    [[nodiscard]] VkDevice getDevice() const { return m_device; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    [[nodiscard]] VkInstance getInstance() const { return m_instance; }
    [[nodiscard]] VkSurfaceKHR getSurface() const { return m_surface; }
    [[nodiscard]] VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] VkQueue getPresentQueue() const { return m_presentQueue; }
    [[nodiscard]] QueueFamilyIndices getQueueFamilies() const { return m_queueFamilyIndices; }

private:
    void createInstance();

    void setupDebugMessenger();

    void createSurface();

    void pickPhysicalDevice();

    void createLogicalDevice();

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;

    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    QueueFamilyIndices m_queueFamilyIndices;

    const std::vector<const char *> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    std::vector<const char *> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};
