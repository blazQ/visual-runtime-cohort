target_sources(visual_runtime PRIVATE
    renderer_vulkan.cpp
    vulkan_context.cpp
    vulkan_frame_resources.cpp
    vulkan_pipeline.cpp
    vulkan_swapchain.cpp
    vulkan_utils.cpp
)

find_package(Vulkan QUIET)

if(Vulkan_FOUND)
    target_link_libraries(visual_runtime PRIVATE Vulkan::Vulkan)
else()
    find_path(VRT_VULKAN_INCLUDE_DIR
        NAMES vulkan/vulkan.h
        HINTS
            "$ENV{VULKAN_SDK}/include"
            /opt/homebrew/opt/vulkan-headers/include
            /usr/local/include
            /usr/include
    )
    find_library(VRT_VULKAN_LIBRARY
        NAMES vulkan
        HINTS
            "$ENV{VULKAN_SDK}/lib"
            /opt/homebrew/opt/vulkan-loader/lib
            /usr/local/lib
            /usr/lib
    )

    if(NOT VRT_VULKAN_INCLUDE_DIR OR NOT VRT_VULKAN_LIBRARY)
        message(FATAL_ERROR "VISUAL_RUNTIME_BACKEND=Vulkan requires Vulkan SDK headers and loader. Install a Vulkan SDK and make it visible to CMake, for example by sourcing the SDK setup-env.sh script.")
    endif()

    target_include_directories(visual_runtime PRIVATE "${VRT_VULKAN_INCLUDE_DIR}")
    target_link_libraries(visual_runtime PRIVATE "${VRT_VULKAN_LIBRARY}")
endif()

find_program(GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        /opt/homebrew/bin
        /usr/local/bin
)

if(NOT GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "VISUAL_RUNTIME_BACKEND=Vulkan requires glslangValidator to compile GLSL shaders. Install a Vulkan SDK, or on macOS run: brew install glslang")
endif()

set(VRT_VULKAN_SHADER_DIR "${CMAKE_SOURCE_DIR}/core/shaders/vulkan")
set(VRT_VULKAN_SPIRV_DIR "${CMAKE_BINARY_DIR}/shaders/vulkan")
set(VRT_VULKAN_VERTEX_SHADER "${VRT_VULKAN_SHADER_DIR}/basic.vert.glsl")
set(VRT_VULKAN_FRAGMENT_SHADER "${VRT_VULKAN_SHADER_DIR}/basic.frag.glsl")
set(VRT_VULKAN_VERTEX_SPV "${VRT_VULKAN_SPIRV_DIR}/basic.vert.spv")
set(VRT_VULKAN_FRAGMENT_SPV "${VRT_VULKAN_SPIRV_DIR}/basic.frag.spv")

add_custom_command(
    OUTPUT "${VRT_VULKAN_VERTEX_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VRT_VULKAN_SPIRV_DIR}"
    COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" -V "${VRT_VULKAN_VERTEX_SHADER}" -o "${VRT_VULKAN_VERTEX_SPV}"
    DEPENDS "${VRT_VULKAN_VERTEX_SHADER}"
    COMMENT "Compiling Vulkan vertex shader ${VRT_VULKAN_VERTEX_SHADER}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${VRT_VULKAN_FRAGMENT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VRT_VULKAN_SPIRV_DIR}"
    COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" -V "${VRT_VULKAN_FRAGMENT_SHADER}" -o "${VRT_VULKAN_FRAGMENT_SPV}"
    DEPENDS "${VRT_VULKAN_FRAGMENT_SHADER}"
    COMMENT "Compiling Vulkan fragment shader ${VRT_VULKAN_FRAGMENT_SHADER}"
    VERBATIM
)

add_custom_target(visual_runtime_vulkan_shaders
    DEPENDS
        "${VRT_VULKAN_VERTEX_SPV}"
        "${VRT_VULKAN_FRAGMENT_SPV}"
)
add_dependencies(visual_runtime visual_runtime_vulkan_shaders)

target_compile_definitions(visual_runtime PRIVATE
    VRT_VULKAN_VERTEX_SPV_PATH="${VRT_VULKAN_VERTEX_SPV}"
    VRT_VULKAN_FRAGMENT_SPV_PATH="${VRT_VULKAN_FRAGMENT_SPV}"
)

target_compile_definitions(visual_runtime PRIVATE
    VRT_BACKEND_VULKAN=1
)
