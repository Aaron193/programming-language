cmake_minimum_required(VERSION 3.10)

# If we use any external dependencies, we would need this
# Set VCPKG_ROOT to the path of your vcpkg installation
# set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

project(interpreter VERSION 1.0)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
            "This project requires GCC or Clang (computed-goto VM dispatch relies on GNU labels-as-values).")
endif()

option(ENABLE_VM_TRACE "Enable VM stack and disassembly trace output" OFF)
option(ENABLE_LTO "Enable link-time optimization for Release builds" OFF)
option(ENABLE_PGO_GENERATE
       "Build with profile generation instrumentation for Release builds" OFF)
option(ENABLE_PGO_USE
       "Build with profile-use optimization for Release builds" OFF)
option(ENABLE_PROFILING
       "Build with profiler-friendly symbols and frame pointers" OFF)
set(PGO_PROFILE_PATH "" CACHE STRING
    "Profile output/input path used by PGO flags")

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

add_executable(interpreter ${SOURCES})
target_include_directories(interpreter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(examples_math_package MODULE
    packages/examples/math/package.cpp
)
target_include_directories(examples_math_package
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
set_target_properties(examples_math_package PROPERTIES
    PREFIX ""
    OUTPUT_NAME "package"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/packages/examples/math"
)

add_library(examples_mismatch_package MODULE
    packages/examples/mismatch/package.cpp
)
target_include_directories(examples_mismatch_package
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
set_target_properties(examples_mismatch_package PROPERTIES
    PREFIX ""
    OUTPUT_NAME "package"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/packages/examples/mismatch"
)

add_library(examples_counter_package MODULE
    packages/examples/counter/package.cpp
)
target_include_directories(examples_counter_package
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
set_target_properties(examples_counter_package PROPERTIES
    PREFIX ""
    OUTPUT_NAME "package"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/packages/examples/counter"
)

add_subdirectory(packages/mog/window)

if(ENABLE_VM_TRACE)
    target_compile_definitions(interpreter PRIVATE VM_TRACE)
endif()

if(ENABLE_PGO_GENERATE AND ENABLE_PGO_USE)
    message(FATAL_ERROR "ENABLE_PGO_GENERATE and ENABLE_PGO_USE are mutually exclusive.")
endif()

if(ENABLE_PROFILING AND (ENABLE_PGO_GENERATE OR ENABLE_PGO_USE))
    message(FATAL_ERROR
            "ENABLE_PROFILING cannot be combined with PGO generation/use.")
endif()

if((ENABLE_PGO_GENERATE OR ENABLE_PGO_USE) AND
   CMAKE_BUILD_TYPE AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "PGO flags are typically intended for Release builds.")
endif()

if(ENABLE_PROFILING AND
   CMAKE_BUILD_TYPE AND
   CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING
            "ENABLE_PROFILING is intended for optimized builds; consider Release.")
endif()

set(EXTRA_RELEASE_COMPILE_FLAGS "")
set(EXTRA_RELEASE_LINK_FLAGS "")

if(ENABLE_PGO_GENERATE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "PGO generation currently supports GNU/Clang compilers.")
    endif()

    if(PGO_PROFILE_PATH)
        list(APPEND EXTRA_RELEASE_COMPILE_FLAGS
             "-fprofile-generate=${PGO_PROFILE_PATH}")
        list(APPEND EXTRA_RELEASE_LINK_FLAGS
             "-fprofile-generate=${PGO_PROFILE_PATH}")
    else()
        list(APPEND EXTRA_RELEASE_COMPILE_FLAGS "-fprofile-generate")
        list(APPEND EXTRA_RELEASE_LINK_FLAGS "-fprofile-generate")
    endif()
endif()

if(ENABLE_PGO_USE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "PGO use currently supports GNU/Clang compilers.")
    endif()
    if(NOT PGO_PROFILE_PATH)
        message(FATAL_ERROR "ENABLE_PGO_USE requires -DPGO_PROFILE_PATH=<path>.")
    endif()

    list(APPEND EXTRA_RELEASE_COMPILE_FLAGS "-fprofile-use=${PGO_PROFILE_PATH}")
    list(APPEND EXTRA_RELEASE_LINK_FLAGS "-fprofile-use=${PGO_PROFILE_PATH}")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND EXTRA_RELEASE_COMPILE_FLAGS "-fprofile-correction")
        list(APPEND EXTRA_RELEASE_LINK_FLAGS "-fprofile-correction")
    endif()
endif()

if(ENABLE_PROFILING)
    list(APPEND EXTRA_RELEASE_COMPILE_FLAGS
         "-g"
         "-O2"
         "-fno-omit-frame-pointer"
         "-fno-inline-functions-called-once")
endif()

# If using Microsoft Visual Studio, set specific compile options
if(MSVC)
    target_compile_options(interpreter PRIVATE
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
else()
    target_compile_options(interpreter PRIVATE
        $<$<CONFIG:Release>:-DNDEBUG>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

foreach(flag IN LISTS EXTRA_RELEASE_COMPILE_FLAGS)
    target_compile_options(interpreter PRIVATE $<$<CONFIG:Release>:${flag}>)
endforeach()

foreach(flag IN LISTS EXTRA_RELEASE_LINK_FLAGS)
    set_property(TARGET interpreter APPEND_STRING PROPERTY
                 LINK_FLAGS_RELEASE " ${flag}")
endforeach()

if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)

    if(NOT LTO_SUPPORTED)
        message(FATAL_ERROR "ENABLE_LTO requested but IPO/LTO is unsupported: ${LTO_ERROR}")
    endif()

    set_property(TARGET interpreter PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
