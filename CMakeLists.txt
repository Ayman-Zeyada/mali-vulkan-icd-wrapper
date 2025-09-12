cmake_minimum_required(VERSION 3.16)
project(mali-extension-wrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(VULKAN REQUIRED vulkan)

# Add compile flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Include directories
include_directories(${VULKAN_INCLUDE_DIRS})
include_directories(src)

# Source files for core wrapper
set(CORE_SOURCES
    src/core/wrapper_main.cpp
    src/core/mali_loader.cpp
    src/core/extension_manager.cpp
    src/core/vulkan_dispatch.cpp
)

# Source files for utilities
set(UTILS_SOURCES
    src/utils/logging.cpp
    src/utils/config.cpp
)

# Source files for extensions
set(EXTENSION_SOURCES
    src/extensions/map_memory_placed/map_memory_placed.cpp
    src/extensions/map_memory_placed/memory_mapper.cpp
    src/extensions/map_memory_placed/address_allocator.cpp
)

# Create the main wrapper library
add_library(mali_extension_wrapper SHARED
    ${CORE_SOURCES}
    ${UTILS_SOURCES}
    ${EXTENSION_SOURCES}
)

# Link libraries
target_link_libraries(mali_extension_wrapper
    ${VULKAN_LIBRARIES}
    dl
    pthread
)

# Set library properties
set_target_properties(mali_extension_wrapper PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "mali_extension_wrapper"
)

# Installation rules
install(TARGETS mali_extension_wrapper
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install ICD manifest
install(FILES config/mali_wrapper.json
    DESTINATION share/vulkan/icd.d
)

# Install configuration
install(FILES config/extensions.conf
    DESTINATION etc/mali-wrapper
)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples (optional)
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()