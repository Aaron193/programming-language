find_package(PkgConfig QUIET)

set(EXPR_MOG_WINDOW_SDL2_FOUND FALSE)
set(EXPR_MOG_WINDOW_INCLUDE_DIRS "")
set(EXPR_MOG_WINDOW_LIBRARY_TARGETS "")
set(EXPR_MOG_WINDOW_LINK_LIBRARIES "")
set(EXPR_MOG_WINDOW_LINK_OPTIONS "")
set(EXPR_MOG_WINDOW_COMPILE_OPTIONS "")

if(PKG_CONFIG_FOUND)
    pkg_check_modules(EXPR_MOG_WINDOW_SDL2 QUIET sdl2)
endif()

if(EXPR_MOG_WINDOW_SDL2_FOUND)
    message(STATUS "SDL2 found via pkg-config; enabling mog:window package")
    list(APPEND EXPR_MOG_WINDOW_INCLUDE_DIRS ${EXPR_MOG_WINDOW_SDL2_INCLUDE_DIRS})
    list(APPEND EXPR_MOG_WINDOW_LINK_LIBRARIES ${EXPR_MOG_WINDOW_SDL2_LINK_LIBRARIES})
    list(APPEND EXPR_MOG_WINDOW_LINK_OPTIONS ${EXPR_MOG_WINDOW_SDL2_LDFLAGS_OTHER})
    list(APPEND EXPR_MOG_WINDOW_COMPILE_OPTIONS ${EXPR_MOG_WINDOW_SDL2_CFLAGS_OTHER})
else()
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        message(STATUS "SDL2 found via CMake; enabling mog:window package")
        list(APPEND EXPR_MOG_WINDOW_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
        if(TARGET SDL2::SDL2)
            list(APPEND EXPR_MOG_WINDOW_LIBRARY_TARGETS SDL2::SDL2)
        else()
            list(APPEND EXPR_MOG_WINDOW_LINK_LIBRARIES ${SDL2_LIBRARIES})
        endif()
    else()
        message(STATUS "SDL2 not found; skipping mog:window package")
        return()
    endif()
endif()

add_library(mog_window_package MODULE
    package.cpp
)
target_include_directories(mog_window_package
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        ${EXPR_MOG_WINDOW_INCLUDE_DIRS}
)
target_compile_options(mog_window_package PRIVATE ${EXPR_MOG_WINDOW_COMPILE_OPTIONS})
target_link_libraries(mog_window_package
    PRIVATE
        ${EXPR_MOG_WINDOW_LIBRARY_TARGETS}
        ${EXPR_MOG_WINDOW_LINK_LIBRARIES}
)
foreach(flag IN LISTS EXPR_MOG_WINDOW_LINK_OPTIONS)
    set_property(TARGET mog_window_package APPEND_STRING PROPERTY
                 LINK_FLAGS " ${flag}")
endforeach()
set_target_properties(mog_window_package PROPERTIES
    PREFIX ""
    OUTPUT_NAME "package"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/packages/mog/window"
)
