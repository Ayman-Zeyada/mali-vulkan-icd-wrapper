cmake_minimum_required(VERSION 3.16)
project(mali_wrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_32BIT "Build 32-bit wrapper" ON)
option(BUILD_64BIT "Build 64-bit wrapper" ON)
option(INSTALL_ICDS "Install ICD manifests" ON)

# Path configuration options
set(MALI_DRIVER_PATH_64 "/usr/lib/aarch64-linux-gnu/libmali-backend.so"
    CACHE STRING "Path to 64-bit Mali driver")
set(MALI_DRIVER_PATH_32 "/usr/lib/arm-linux-gnueabihf/libmali-backend.so"
    CACHE STRING "Path to 32-bit Mali driver")

set(WSI_LAYER_PATH_64 "/usr/lib/aarch64-linux-gnu/vulkan/implicit_layer.d/libVkLayer_window_system_integration.so"
    CACHE STRING "Path to 64-bit WSI layer")
set(WSI_LAYER_PATH_32 "/usr/lib/arm-linux-gnueabihf/vulkan/implicit_layer.d/libVkLayer_window_system_integration.so"
    CACHE STRING "Path to 32-bit WSI layer")

# API version
set(API_VERSION "1.3.276" CACHE STRING "Vulkan API version")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(VULKAN REQUIRED vulkan)

# Common source files
set(COMMON_SOURCES
    src/icd_main.cpp
    src/core/mali_wrapper_icd.cpp
    src/core/library_loader.cpp
    src/utils/logging.cpp
)

# Common include directories
set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VULKAN_INCLUDE_DIRS}
)

# Common compile definitions
set(COMMON_DEFINITIONS
    VK_USE_PLATFORM_XLIB_KHR
    VK_USE_PLATFORM_WAYLAND_KHR
)

# Common link libraries
set(COMMON_LIBRARIES
    ${CMAKE_DL_LIBS}
)

# Function to create a wrapper target
function(create_wrapper_target ARCH_NAME ARCH_FLAGS)
    set(TARGET_NAME mali_wrapper_${ARCH_NAME})

    # Set architecture-specific paths
    if(ARCH_NAME STREQUAL "aarch64")
        set(MALI_DRIVER_PATH ${MALI_DRIVER_PATH_64})
        set(WSI_LAYER_PATH ${WSI_LAYER_PATH_64})
        set(INSTALL_DIR "lib/aarch64-linux-gnu")
    elseif(ARCH_NAME STREQUAL "armhf")
        set(MALI_DRIVER_PATH ${MALI_DRIVER_PATH_32})
        set(WSI_LAYER_PATH ${WSI_LAYER_PATH_32})
        set(INSTALL_DIR "lib/arm-linux-gnueabihf")
    else()
        message(FATAL_ERROR "Unknown architecture: ${ARCH_NAME}")
    endif()

    # Generate configuration header for this architecture
    configure_file(
        ${CMAKE_SOURCE_DIR}/cmake/config.hpp.in
        ${CMAKE_BINARY_DIR}/include/${ARCH_NAME}/config.hpp
        @ONLY
    )

    add_library(${TARGET_NAME} SHARED ${COMMON_SOURCES})

    # Add architecture-specific include directory (for config.hpp)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${COMMON_INCLUDES}
        ${CMAKE_BINARY_DIR}/include/${ARCH_NAME}
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${TARGET_NAME} PRIVATE ${COMMON_LIBRARIES})

    # Set architecture-specific flags
    if(ARCH_FLAGS)
        target_compile_options(${TARGET_NAME} PRIVATE ${ARCH_FLAGS})
        target_link_options(${TARGET_NAME} PRIVATE ${ARCH_FLAGS})
    endif()

    # Set output name
    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "mali_wrapper"
        SOVERSION 1
        VERSION 1.0.0
    )

    # Install library to architecture-specific directory
    install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION ${INSTALL_DIR}
        ARCHIVE DESTINATION ${INSTALL_DIR}
    )
endfunction()

# Detect current architecture (may be overridden by toolchain)
if(NOT DEFINED CURRENT_ARCH)
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE CURRENT_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Build 64-bit wrapper
if(BUILD_64BIT)
    if(CURRENT_ARCH STREQUAL "aarch64")
        create_wrapper_target("aarch64" "")
        message(STATUS "Building 64-bit wrapper for aarch64")
    else()
        message(WARNING "64-bit build requested but not on aarch64 platform")
    endif()
endif()

# Create standalone target that builds single architecture (for cross-compilation)
if(NOT BUILD_32BIT AND NOT BUILD_64BIT)
    # If no specific build is requested, build for current architecture
    if(CURRENT_ARCH STREQUAL "aarch64")
        create_wrapper_target("aarch64" "")
        message(STATUS "Building standalone wrapper for aarch64")
    elseif(CURRENT_ARCH MATCHES "^arm")
        create_wrapper_target("armhf" "")
        message(STATUS "Building standalone wrapper for armhf")
    endif()
endif()

# Build 32-bit wrapper (requires cross-compilation on 64-bit systems)
if(BUILD_32BIT)
    if(CURRENT_ARCH STREQUAL "aarch64")
        # Check for 32-bit cross-compilation toolchain
        find_program(ARM_GCC arm-linux-gnueabihf-gcc)
        find_program(ARM_GPP arm-linux-gnueabihf-g++)

        if(ARM_GCC AND ARM_GPP)
            message(STATUS "32-bit cross-compilation detected but will be handled via separate build")
            message(STATUS "To build 32-bit version, run:")
            message(STATUS "  mkdir build-armhf && cd build-armhf")
            message(STATUS "  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/armhf_toolchain.cmake")
            message(STATUS "  make")
        else()
            message(WARNING "32-bit build requested but arm-linux-gnueabihf toolchain not found")
            message(STATUS "Install with: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf")
        endif()
    elseif(CURRENT_ARCH MATCHES "^arm")
        create_wrapper_target("armhf" "")
        message(STATUS "Building 32-bit wrapper for armhf (native)")
    else()
        message(WARNING "32-bit build requested but not on ARM platform")
    endif()
endif()

# No configuration file needed - paths are compiled in

# Generate and install ICD manifests
if(INSTALL_ICDS)
    # 64-bit ICD manifest
    if(BUILD_64BIT)
        configure_file(
            ${CMAKE_SOURCE_DIR}/cmake/manifests/mali_icd.aarch64.json.in
            ${CMAKE_BINARY_DIR}/mali_icd.aarch64.json
            @ONLY
        )
        install(FILES ${CMAKE_BINARY_DIR}/mali_icd.aarch64.json
            DESTINATION etc/vulkan/icd.d
        )
    endif()

    # 32-bit ICD manifest
    if(BUILD_32BIT)
        configure_file(
            ${CMAKE_SOURCE_DIR}/cmake/manifests/mali_icd.armhf.json.in
            ${CMAKE_BINARY_DIR}/mali_icd.armhf.json
            @ONLY
        )
        install(FILES ${CMAKE_BINARY_DIR}/mali_icd.armhf.json
            DESTINATION etc/vulkan/icd.d
        )
    endif()
endif()

# Print build summary
message(STATUS "Mali Wrapper ICD Configuration:")
message(STATUS "  64-bit wrapper: ${BUILD_64BIT}")
message(STATUS "  32-bit wrapper: ${BUILD_32BIT}")
message(STATUS "  Install ICDs: ${INSTALL_ICDS}")
message(STATUS "  Current architecture: ${CURRENT_ARCH}")