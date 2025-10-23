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
#include <span>
#include <string>
#include <vector>

#define VK_CHECK(x)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        VkResult error = x;                                                                                            \
        if (error)                                                                                                     \
        {                                                                                                              \
            std::cout << format("Detected Vulkan error: {}", string_VkResult(err)) << std::endl;                       \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
