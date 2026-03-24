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
option(ENABLE_ASAN "Build with AddressSanitizer instrumentation" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer instrumentation" OFF)
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

set(CORE_SOURCES ${SOURCES})
list(REMOVE_ITEM CORE_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(interpreter_core STATIC ${CORE_SOURCES})
target_include_directories(interpreter_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(interpreter
    src/main.cpp
)
target_link_libraries(interpreter PRIVATE interpreter_core)
target_include_directories(interpreter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(ast_parser_smoke
    tests/ast_parser_smoke.cpp
)
target_include_directories(ast_parser_smoke
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(ast_parser_smoke PRIVATE interpreter_core)

add_executable(ast_emitter_regression
    tests/ast_emitter_regression.cpp
)
target_include_directories(ast_emitter_regression
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(ast_emitter_regression PRIVATE interpreter_core)

add_executable(ast_frontend_regression
    tests/ast_frontend_regression.cpp
)
target_include_directories(ast_frontend_regression
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(ast_frontend_regression PRIVATE interpreter_core)

add_executable(frontend_benchmark
    tests/frontend_benchmark.cpp
)
target_include_directories(frontend_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(frontend_benchmark PRIVATE interpreter_core)

add_executable(frontend_tooling_regression
    tests/frontend_tooling_regression.cpp
)
target_include_directories(frontend_tooling_regression
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(frontend_tooling_regression PRIVATE interpreter_core)

add_executable(mog-lsp
    tooling/lsp/mog-lsp.cpp
)
target_include_directories(mog-lsp
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(mog-lsp PRIVATE interpreter_core)

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
    target_compile_definitions(interpreter_core PRIVATE VM_TRACE)
endif()

if((ENABLE_ASAN OR ENABLE_UBSAN) AND ENABLE_LTO)
    message(FATAL_ERROR
            "Sanitizers cannot be combined with ENABLE_LTO in this build.")
endif()

if((ENABLE_ASAN OR ENABLE_UBSAN) AND (ENABLE_PGO_GENERATE OR ENABLE_PGO_USE))
    message(FATAL_ERROR
            "Sanitizers cannot be combined with PGO generation/use.")
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
set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
    list(APPEND SANITIZER_COMPILE_FLAGS
         "-fsanitize=address"
         "-fno-omit-frame-pointer")
    list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=address")
endif()

if(ENABLE_UBSAN)
    list(APPEND SANITIZER_COMPILE_FLAGS
         "-fsanitize=undefined"
         "-fno-omit-frame-pointer")
    list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=undefined")
endif()

function(apply_common_compile_options target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:/O2 /DNDEBUG>
            $<$<CONFIG:Debug>:/Od /Zi>
        )
    else()
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:-DNDEBUG>
            $<$<CONFIG:Debug>:-g -O0>
        )
        if(SANITIZER_COMPILE_FLAGS)
            target_compile_options(${target_name} PRIVATE
                ${SANITIZER_COMPILE_FLAGS}
            )
        endif()
    endif()
endfunction()

function(apply_common_link_flags target_name)
    foreach(flag IN LISTS SANITIZER_LINK_FLAGS)
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                     LINK_FLAGS " ${flag}")
    endforeach()
    foreach(flag IN LISTS EXTRA_RELEASE_LINK_FLAGS)
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                     LINK_FLAGS_RELEASE " ${flag}")
    endforeach()
endfunction()

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

set(PROJECT_COMPILE_TARGETS
    interpreter_core
    interpreter
    ast_parser_smoke
    ast_emitter_regression
    ast_frontend_regression
    frontend_benchmark
    frontend_tooling_regression
    mog-lsp
    examples_math_package
    examples_mismatch_package
    examples_counter_package
)

if(TARGET mog_window_package)
    list(APPEND PROJECT_COMPILE_TARGETS mog_window_package)
endif()

foreach(target_name IN LISTS PROJECT_COMPILE_TARGETS)
    apply_common_compile_options(${target_name})
endforeach()

foreach(flag IN LISTS EXTRA_RELEASE_COMPILE_FLAGS)
    foreach(target_name IN LISTS PROJECT_COMPILE_TARGETS)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:${flag}>
        )
    endforeach()
endforeach()

set(PROJECT_LINK_TARGETS
    interpreter
    ast_parser_smoke
    ast_emitter_regression
    ast_frontend_regression
    frontend_benchmark
    frontend_tooling_regression
    mog-lsp
    examples_math_package
    examples_mismatch_package
    examples_counter_package
)

if(TARGET mog_window_package)
    list(APPEND PROJECT_LINK_TARGETS mog_window_package)
endif()

foreach(target_name IN LISTS PROJECT_LINK_TARGETS)
    apply_common_link_flags(${target_name})
endforeach()

if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)

    if(NOT LTO_SUPPORTED)
        message(FATAL_ERROR "ENABLE_LTO requested but IPO/LTO is unsupported: ${LTO_ERROR}")
    endif()

    set_property(TARGET interpreter_core PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET interpreter PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET mog-lsp PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
