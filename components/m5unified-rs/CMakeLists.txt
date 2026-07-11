cmake_minimum_required(VERSION 3.16)

# ESP-IDF component wrapper for the Rust-facing M5Unified C ABI shim.
#
# This component is meant to be copied or symlinked into an esp-idf-sys project
# component directory, or included by a consuming ESP-IDF CMake build. It keeps
# the Rust FFI boundary small: Rust links to the flat C functions declared in
# m5u_shim.h, while the default C++ source calls the real M5Unified API.
#
# Firmware builds should use the real shim. The stub source is available only
# for host-side C ABI checks that intentionally do not link M5Unified.
option(M5UNIFIED_RS_USE_HOST_STUB "Build the no-op host C ABI shim instead of the firmware shim" OFF)

if(M5UNIFIED_RS_USE_HOST_STUB)
    set(M5UNIFIED_RS_SHIM_SRCS "m5u_shim_stub.cpp")
    set(M5UNIFIED_RS_COMPONENT_REQUIRES "")
else()
    set(M5UNIFIED_RS_SHIM_SRCS "m5u_shim.cpp")
    set(M5UNIFIED_RS_COMPONENT_REQUIRES M5Unified M5GFX esp_timer driver fatfs sdmmc)
    if(IDF_VERSION_MAJOR GREATER 5 OR (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
        list(APPEND M5UNIFIED_RS_COMPONENT_REQUIRES esp_driver_sdspi esp_driver_spi)
    endif()
endif()

idf_component_register(
    SRCS ${M5UNIFIED_RS_SHIM_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${M5UNIFIED_RS_COMPONENT_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
