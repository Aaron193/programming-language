cmake_minimum_required(VERSION 3.10)

# If we use any external dependencies, we would need this
# Set VCPKG_ROOT to the path of your vcpkg installation
# set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

project(interpreter VERSION 1.0)

option(ENABLE_VM_TRACE "Enable VM stack and disassembly trace output" OFF)
option(ENABLE_LTO "Enable link-time optimization for Release builds" OFF)
option(ENABLE_PGO_GENERATE
       "Build with profile generation instrumentation for Release builds" OFF)
option(ENABLE_PGO_USE
       "Build with profile-use optimization for Release builds" OFF)
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

if(ENABLE_VM_TRACE)
    target_compile_definitions(interpreter PRIVATE VM_TRACE)
endif()

if(ENABLE_PGO_GENERATE AND ENABLE_PGO_USE)
    message(FATAL_ERROR "ENABLE_PGO_GENERATE and ENABLE_PGO_USE are mutually exclusive.")
endif()

if((ENABLE_PGO_GENERATE OR ENABLE_PGO_USE) AND
   CMAKE_BUILD_TYPE AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "PGO flags are typically intended for Release builds.")
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

# If using Microsoft Visual Studio, set specific compile options
if(MSVC)
    target_compile_options(interpreter PRIVATE
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
else()
    target_compile_options(interpreter PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
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
