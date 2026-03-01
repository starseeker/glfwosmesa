cmake_minimum_required(VERSION 3.15)
project(glfwosmesa VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(ExternalProject)

# Use NEW timestamp policy for URL downloads when the CMake version supports it.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ---------------------------------------------------------------------------
# Tools required to build the local dependencies
# ---------------------------------------------------------------------------

find_program(PATCH_EXECUTABLE patch)
if(NOT PATCH_EXECUTABLE)
    message(FATAL_ERROR "patch not found. Install the 'patch' package.")
endif()

find_program(MESON_EXECUTABLE meson)
if(NOT MESON_EXECUTABLE)
    message(FATAL_ERROR "meson not found. Install meson (pip install meson or package manager).")
endif()

find_program(NINJA_EXECUTABLE ninja)
if(NOT NINJA_EXECUTABLE)
    message(FATAL_ERROR "ninja not found. Install ninja-build.")
endif()

# ---------------------------------------------------------------------------
# Local build: GLFW 3.3.10 with glfwBlitPixelBuffer patch applied
# ---------------------------------------------------------------------------

set(GLFW_VERSION     "3.3.10")
set(GLFW_PREFIX      "${CMAKE_CURRENT_BINARY_DIR}/glfw-local")
set(GLFW_LIBRARY     "${GLFW_PREFIX}/lib/libglfw3.a")
set(GLFW_INCLUDE_DIR "${GLFW_PREFIX}/include")

# Create the directory now so CMake accepts it as an INTERFACE include path
# before the external project has been built.
file(MAKE_DIRECTORY "${GLFW_INCLUDE_DIR}")

ExternalProject_Add(glfw_ep
    URL "https://github.com/glfw/glfw/archive/refs/tags/${GLFW_VERSION}.tar.gz"
    PATCH_COMMAND
        ${PATCH_EXECUTABLE} -p1 -d <SOURCE_DIR>
            -i "${CMAKE_CURRENT_SOURCE_DIR}/glfw-patches/0001-Add-glfwBlitPixelBuffer.patch"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        -DGLFW_BUILD_DOCS=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_INSTALL_LIBDIR=lib
        "-DCMAKE_INSTALL_PREFIX=${GLFW_PREFIX}"
    BUILD_BYPRODUCTS "${GLFW_LIBRARY}"
)

add_library(glfw_local STATIC IMPORTED GLOBAL)
add_dependencies(glfw_local glfw_ep)
set_target_properties(glfw_local PROPERTIES
    IMPORTED_LOCATION             "${GLFW_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}"
)

# ---------------------------------------------------------------------------
# Local build: Mesa OSMesa (softpipe — pure CPU, no GPU driver needed)
# ---------------------------------------------------------------------------

set(MESA_VERSION        "23.3.6")
set(OSMESA_PREFIX       "${CMAKE_CURRENT_BINARY_DIR}/osmesa-local")
set(OSMESA_INCLUDE_DIRS "${OSMESA_PREFIX}/include")

file(MAKE_DIRECTORY "${OSMESA_INCLUDE_DIRS}")

ExternalProject_Add(osmesa_ep
    URL "https://archive.mesa3d.org/mesa-${MESA_VERSION}.tar.xz"
    CONFIGURE_COMMAND
        ${MESON_EXECUTABLE} setup <BINARY_DIR> <SOURCE_DIR>
            "--prefix=${OSMESA_PREFIX}"
            "--libdir=lib"
            --buildtype=release
            -Dosmesa=true
            -Dgallium-drivers=softpipe
            -Dvulkan-drivers=
            -Dglx=disabled
            -Degl=disabled
            -Dgbm=disabled
            -Dplatforms=
            -Dglvnd=false
    BUILD_COMMAND   ${NINJA_EXECUTABLE} -C <BINARY_DIR>
    INSTALL_COMMAND ${NINJA_EXECUTABLE} -C <BINARY_DIR> install
    # Track only the stable unversioned symlink; its target is the real library.
    BUILD_BYPRODUCTS "${OSMESA_PREFIX}/lib/libOSMesa.so"
)

# INTERFACE target: link against the unversioned .so symlink (linker-stable
# across Mesa releases); BUILD_RPATH on consumers ensures the versioned
# library is found at runtime without LD_LIBRARY_PATH.
add_library(osmesa_local INTERFACE)
add_dependencies(osmesa_local osmesa_ep)
target_include_directories(osmesa_local INTERFACE "${OSMESA_INCLUDE_DIRS}")
target_link_libraries(osmesa_local INTERFACE "${OSMESA_PREFIX}/lib/libOSMesa.so")

# ---------------------------------------------------------------------------
# X11 — still needed to compile the platform fallback code in glfw_osmesa.c
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

# Ensure the source compiles only after both external projects are installed.
add_dependencies(glfw_osmesa glfw_ep osmesa_ep)

target_include_directories(glfw_osmesa
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${OSMESA_INCLUDE_DIRS}"
        "${GLFW_INCLUDE_DIR}"
)

target_link_libraries(glfw_osmesa
    PUBLIC
        glfw_local
    PRIVATE
        osmesa_local
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
    target_link_libraries(triangle PRIVATE glfw_osmesa osmesa_local)
    if(UNIX AND NOT APPLE)
        target_link_libraries(triangle PRIVATE m)
        # Embed the local OSMesa lib path so the binary runs without LD_LIBRARY_PATH.
        set_target_properties(triangle PROPERTIES
            BUILD_RPATH "${OSMESA_PREFIX}/lib"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS glfw_osmesa
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/glfw_osmesa.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
