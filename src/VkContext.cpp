#include "VkContext.hpp"

#include <set>

#include <SDL3/SDL_vulkan.h>

#include "VkEngine.hpp"
#include "VkTypes.hpp"

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *
) {
    std::cerr << "validation layer: type " << std::to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void VulkanContext::init() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
}

void VulkanContext::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device); // Ensure GPU is done before destruction
    }

    // Destroy logical device
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

#ifndef NDEBUG
    // Destroy debug messenger (if validation layers are enabled)
    if (m_debugMessenger != VK_NULL_HANDLE) {
        const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT")
        );

        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }

        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    // Destroy surface (created with glfwCreateWindowSurface)
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    // Destroy Vulkan instance
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::createInstance() {
    constexpr VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Renderer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Hellfire",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    // Get required instance level layers
    std::vector<const char *> requiredLayers{};

    // Get the required instance extensions from SDL3
    uint32_t sdlExtensionCount = 0;
    const char *const*sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    std::vector requiredExtensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

#if defined(__APPLE__)
    requiredExtensions.push_back(vk::KHRPortabilityEnumerationExtensionName);
#endif

#ifndef NDEBUG
    requiredLayers.assign(m_validationLayers.begin(), m_validationLayers.end());
    requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data()
    };

#if defined(__APPLE__)
    createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
}

void VulkanContext::setupDebugMessenger() {
#ifndef NDEBUG
    constexpr VkDebugUtilsMessengerCreateInfoEXT createInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback
    };

    const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT")
    );

    if (func != nullptr) {
        func(m_instance, &createInfo, nullptr, &m_debugMessenger);
    }
#endif
}

void VulkanContext::createSurface() {
    if (!SDL_Vulkan_CreateSurface(VulkanEngine::Get().getWindow(), m_instance, nullptr, &m_surface)) {
        throw std::runtime_error("failed to create window surface!");
    }

    if (m_surface == VK_NULL_HANDLE) {
        throw std::runtime_error("SDL returned VK_NULL_HANDLE for surface");
    }
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for (const auto &device: devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

void VulkanContext::createLogicalDevice() {
    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set uniqueQueueFamilies = {
        m_queueFamilyIndices.graphicsFamily.value(),
        m_queueFamilyIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily: uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        };
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    // Vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };

    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    // Link feature chains
    features12.pNext = &features13;

    VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features12, // chain starts here
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size()),
        .ppEnabledExtensionNames = m_deviceExtensions.data(),
        .pEnabledFeatures = &deviceFeatures
    };

    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

    vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
}

QueueFamilyIndices VulkanContext::findQueueFamilies(const VkPhysicalDevice device) const {
    QueueFamilyIndices indices = {};

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // Find graphics queue family
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
            break;
        }
    }

    // Check if that family also supports present
    VkBool32 presentSupport = VK_FALSE;
    if (indices.graphicsFamily.has_value()) {
        vkGetPhysicalDeviceSurfaceSupportKHR(device, indices.graphicsFamily.value(), m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = indices.graphicsFamily.value();
        }
    }

    // If not, find a separate present queue
    if (!indices.presentFamily.has_value()) {
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &support);
            if (support) {
                indices.presentFamily = i;
                break;
            }
        }
    }

    return indices;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) const {
    // Check API version support
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    bool supportVulkan13 = deviceProperties.apiVersion >= VK_API_VERSION_1_3;

    // Queue family suitability
    QueueFamilyIndices indices = findQueueFamilies(device);

    // Check device extension support
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    bool supportDeviceExtensions = true;
    for (const char *required: m_deviceExtensions) {
        bool found = false;
        for (const auto &[extensionName, specVersion]: availableExtensions) {
            if (strcmp(required, extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            supportDeviceExtensions = false;
            break;
        }
    }

    // Query for feature support (1.2 + 1.3 + extended dynamic state)
    VkPhysicalDeviceFeatures2 features2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };

    VkPhysicalDeviceVulkan11Features features11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
    };

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicStateFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT
    };

    features2.pNext = &features11;
    features11.pNext = &features13;
    features13.pNext = &dynamicStateFeatures;

    vkGetPhysicalDeviceFeatures2(device, &features2);

    bool supportFeatures = features2.features.samplerAnisotropy &&
                           features11.shaderDrawParameters &&
                           features13.dynamicRendering &&
                           features13.synchronization2 &&
                           dynamicStateFeatures.extendedDynamicState;

    return supportVulkan13 &&
           supportDeviceExtensions &&
           supportFeatures &&
           indices.isComplete();
}
