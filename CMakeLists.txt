cmake_minimum_required(VERSION 3.15)
# CXX is required because osmesa/CMakeLists.txt uses check_cxx_compiler_flag
# to probe for -O2/-O3 optimisation flags.
project(glfwosmesa VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Local build: osmesa (Mesa 7.0.4 extracted C library)
# ---------------------------------------------------------------------------

# osmesa/src/CMakeLists.txt references BIN_DIR and LIB_DIR in install rules.
set(BIN_DIR "${CMAKE_INSTALL_BINDIR}")
set(LIB_DIR "${CMAKE_INSTALL_LIBDIR}")

# Don't build the osmesa examples from the subdirectory.
set(OSMESA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Disable name mangling so the library exports standard gl* names
# (rather than mgl* prefixed names) for compatibility with standard GL code.
set(OSMESA_NAME_MANGLING OFF CACHE BOOL "" FORCE)

add_subdirectory(osmesa)

# ---------------------------------------------------------------------------
# Local build: GLFW 3.4 with glfwBlitPixelBuffer patch applied
# ---------------------------------------------------------------------------

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
# Disable Wayland: wayland-scanner is not available in this build environment.
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)

# ---------------------------------------------------------------------------
# X11 — needed to compile the platform fallback code in glfw_osmesa.c
# ---------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Library target: glfw_osmesa
# ---------------------------------------------------------------------------

add_library(glfw_osmesa STATIC
    src/glfw_osmesa.c
)

target_include_directories(glfw_osmesa
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osmesa/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include"
)

target_link_libraries(glfw_osmesa
    PUBLIC
        glfw
        osmesa
)

if(UNIX AND NOT APPLE)
    target_link_libraries(glfw_osmesa PRIVATE ${X11_LIBRARIES})
    target_include_directories(glfw_osmesa PRIVATE ${X11_INCLUDE_DIR})
elseif(APPLE)
    target_link_libraries(glfw_osmesa PRIVATE
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework AppKit"
    )
elseif(WIN32)
    target_link_libraries(glfw_osmesa PRIVATE gdi32)
endif()

# Promote warnings as errors only for this project's own sources
if(MSVC)
    target_compile_options(glfw_osmesa PRIVATE /W4)
else()
    target_compile_options(glfw_osmesa PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Example executable
# ---------------------------------------------------------------------------

option(GLFWOSMESA_BUILD_EXAMPLES "Build example programs" ON)

if(GLFWOSMESA_BUILD_EXAMPLES)
    add_executable(triangle examples/triangle.c)
    target_link_libraries(triangle PRIVATE glfw_osmesa)
    if(UNIX AND NOT APPLE)
        target_link_libraries(triangle PRIVATE m)
        # Embed the local osmesa lib path so the binary runs without LD_LIBRARY_PATH.
        set_target_properties(triangle PROPERTIES
            BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}/osmesa/src"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------

install(TARGETS glfw_osmesa
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/glfw_osmesa.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
