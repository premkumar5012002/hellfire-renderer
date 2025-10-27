cmake_minimum_required(VERSION 3.20)

project(Hellfire-Renderer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(Dependencies.cmake)

add_executable(${PROJECT_NAME}
        src/Main.cpp
        src/VkEngine.cpp
        src/VkContext.cpp
        src/VkSwapChain.cpp
        src/VkImage.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${stb_SOURCE_DIR}
        ${vma_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
        Vulkan::Vulkan
        SDL3::SDL3
        glm
        fastgltf
        imgui_backend
)