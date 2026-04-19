cmake_minimum_required(VERSION 3.25)

set(MINI_OS_BUILD_MODE host CACHE STRING "Build mode: host or kernel")
set_property(CACHE MINI_OS_BUILD_MODE PROPERTY STRINGS host kernel)
if(NOT MINI_OS_BUILD_MODE MATCHES "^(host|kernel)$")
  message(FATAL_ERROR "MINI_OS_BUILD_MODE must be host or kernel")
endif()

# Expose the host SDK in compile_commands.json as well as the compiler driver.
# clang-tidy does not inherit Homebrew Clang's implicit SDK configuration.
if(MINI_OS_BUILD_MODE STREQUAL "host" AND CMAKE_HOST_APPLE AND
   NOT CMAKE_OSX_SYSROOT AND NOT DEFINED ENV{SDKROOT})
  set(CMAKE_OSX_SYSROOT macosx CACHE STRING "macOS host SDK" FORCE)
endif()

# vcpkg reads features while the toolchain is loaded by project().
option(BUILD_TESTING "Enable tests for the selected build mode" ON)
if(BUILD_TESTING AND MINI_OS_BUILD_MODE STREQUAL "host")
  list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()
project(mini_os VERSION 0.1.0 LANGUAGES C CXX)

if(MINI_OS_BUILD_MODE STREQUAL "kernel")
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Generic" OR NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Kernel builds require the aarch64-none-elf toolchain; use a kernel preset")
  endif()
  enable_language(ASM)
endif()

# Only this architecture/platform combination is supported.
set(ARCH aarch64 CACHE STRING "Kernel architecture")
set(PLATFORM qemu_virt CACHE STRING "Kernel platform")
if(NOT ARCH STREQUAL "aarch64" OR NOT PLATFORM STREQUAL "qemu_virt")
  message(FATAL_ERROR "Only ARCH=aarch64 and PLATFORM=qemu_virt are supported")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MINI_OS_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
option(MINI_OS_SANITIZERS "Enable host AddressSanitizer and UBSan" OFF)
add_library(mini_os_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(mini_os_options INTERFACE
    "$<$<COMPILE_LANGUAGE:C,CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow>")
  if(MINI_OS_WARNINGS_AS_ERRORS)
    target_compile_options(mini_os_options INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:-Werror>")
  endif()
elseif(MSVC)
  target_compile_options(mini_os_options INTERFACE /W4)
  if(MINI_OS_WARNINGS_AS_ERRORS)
    target_compile_options(mini_os_options INTERFACE /WX)
  endif()
endif()

if(MINI_OS_SANITIZERS)
  if(MINI_OS_BUILD_MODE STREQUAL "kernel")
    message(FATAL_ERROR "Sanitizers are supported only in host builds")
  endif()
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
     NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR WIN32)
    message(FATAL_ERROR "The sanitizer preset requires Clang or GCC on macOS/Linux")
  endif()
  target_compile_options(mini_os_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(mini_os_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(mini_os_core STATIC src/kernel/alignment.c)
target_include_directories(mini_os_core PUBLIC include)
target_link_libraries(mini_os_core PRIVATE mini_os_options)

include(CTest)
if(MINI_OS_BUILD_MODE STREQUAL "host")
  include(cmake/host.cmake)
else()
  include(cmake/kernel.cmake)
endif()
