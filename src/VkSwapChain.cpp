#include "VkSwapChain.hpp"

#include <algorithm>

#include <SDL3/SDL.h>

#include "VKEngine.hpp"
#include "VkTypes.hpp"

VulkanSwapChain::VulkanSwapChain(VulkanContext *ctx) : m_ctx(ctx) {
}

void VulkanSwapChain::init() {
    createSwapChain();
    createImageViews();
}

void VulkanSwapChain::recreate() {
    cleanup();
    createSwapChain();
    createImageViews();
}

void VulkanSwapChain::cleanup() const {
    for (const auto imageView: m_imageViews) {
        vkDestroyImageView(m_ctx->getDevice(), imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_ctx->getDevice(), m_swapChain, nullptr);
}

void VulkanSwapChain::createSwapChain() {
    auto [capabilities, formats, presentModes] = querySwapChainSupport(m_ctx->getPhysicalDevice());

    const auto [format, colorSpace] = chooseSwapSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    const VkExtent2D extent = chooseSwapExtent(capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_ctx->getSurface(),
        .minImageCount = imageCount,
        .imageFormat = format,
        .imageColorSpace = colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = m_swapChain
    };

    const auto [graphicsFamily, presentFamily] = m_ctx->getQueueFamilies();
    const uint32_t queueFamilyIndices[] = {graphicsFamily.value(), presentFamily.value()};
    if (graphicsFamily != presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(m_ctx->getDevice(), &createInfo, nullptr, &m_swapChain));

    vkGetSwapchainImagesKHR(m_ctx->getDevice(), m_swapChain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_ctx->getDevice(), m_swapChain, &imageCount, m_images.data());

    m_imageFormat = format;
    m_extent = extent;
}

void VulkanSwapChain::createImageViews() {
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_imageFormat,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        VK_CHECK(vkCreateImageView(m_ctx->getDevice(), &createInfo, nullptr, &m_imageViews[i]));
    }
}

SwapChainSupportDetails VulkanSwapChain::querySwapChainSupport(const VkPhysicalDevice device) const {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device,
        m_ctx->getSurface(),
        &details.capabilities
    );

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device,
        m_ctx->getSurface(),
        &formatCount, nullptr
    );

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device,
            m_ctx->getSurface(),
            &formatCount,
            details.formats.data()
        );
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_ctx->getSurface(), &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, m_ctx->getSurface(), &presentModeCount,
            details.presentModes.data()
        );
    }

    return details;
}

VkExtent2D VulkanSwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width, height;
    SDL_GetWindowSizeInPixels(VulkanEngine::Get().getWindow(), &width, &height);

    return {
        std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
}

VkSurfaceFormatKHR VulkanSwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto &availableFormat: availableFormats) {
        if (
            availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        ) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VulkanSwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto &availablePresentMode: availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}





