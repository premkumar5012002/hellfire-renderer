#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "VkContext.hpp"

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanSwapChain {
public:
    explicit VulkanSwapChain(VulkanContext *ctx);

    [[nodiscard]] const VkSwapchainKHR &getSwapChain() const { return m_swapChain; }
    [[nodiscard]] VkFormat getImageFormat() const { return m_imageFormat; }
    [[nodiscard]] VkExtent2D getExtent() const { return m_extent; }
    [[nodiscard]] const std::vector<VkImage> &getImages() const { return m_images; }
    [[nodiscard]] const std::vector<VkImageView> &getImageViews() const { return m_imageViews; }

    void init();

    void recreate();

    void cleanup() const;

private:
    void createSwapChain();

    void createImageViews();

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;

    static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);

    static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);

    static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

    VulkanContext *m_ctx = nullptr;
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{0, 0};
};
