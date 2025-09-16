# Cross-compilation toolchain for ARM hard-float (armhf)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Override the architecture detection for this build
set(CMAKE_CROSSCOMPILING TRUE)
execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CURRENT_ARCH "armv7l" CACHE STRING "Override architecture for cross-compilation" FORCE)

# Specify the cross compiler
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Set compiler flags for armhf
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")

# Where is the target environment
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# For libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Force standalone build for this toolchain (not 32BIT/64BIT)
set(BUILD_32BIT OFF CACHE BOOL "Build 32-bit wrapper" FORCE)
set(BUILD_64BIT OFF CACHE BOOL "Build 64-bit wrapper" FORCE)