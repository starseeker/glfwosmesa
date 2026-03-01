cmake_minimum_required(VERSION 3.15)
project(glfwosmesa VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Find required dependencies
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)

# --- GLFW -------------------------------------------------------------------
# find_package(glfw3) exports the target named 'glfw' (not glfw3::glfw3).
# pkg_check_modules falls back to plain library names.
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    # Canonical imported target provided by the system glfw3 CMake config.
    set(GLFW_TARGET glfw)
elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
    add_library(glfw3_imported INTERFACE)
    target_include_directories(glfw3_imported INTERFACE ${GLFW3_INCLUDE_DIRS})
    target_link_libraries(glfw3_imported INTERFACE ${GLFW3_LINK_LIBRARIES})
    set(GLFW_TARGET glfw3_imported)
else()
    # Last-resort manual search
    find_path(_GLFW_INC GLFW/glfw3.h
        HINTS /usr/include /usr/local/include)
    find_library(_GLFW_LIB NAMES glfw glfw3
        HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
    if(NOT _GLFW_INC OR NOT _GLFW_LIB)
        message(FATAL_ERROR "GLFW3 not found. "
            "Install libglfw3-dev (Debian/Ubuntu) or glfw-devel (Fedora).")
    endif()
    add_library(glfw3_imported INTERFACE)
    target_include_directories(glfw3_imported INTERFACE ${_GLFW_INC})
    target_link_libraries(glfw3_imported INTERFACE ${_GLFW_LIB})
    set(GLFW_TARGET glfw3_imported)
endif()

# --- OSMesa -----------------------------------------------------------------
if(PKG_CONFIG_FOUND)
    pkg_check_modules(OSMESA QUIET osmesa)
endif()

if(NOT OSMESA_FOUND)
    find_path(OSMESA_INCLUDE_DIRS GL/osmesa.h
        HINTS /usr/include /usr/local/include)
    find_library(OSMESA_LIBRARIES
        NAMES OSMesa osmesa
        HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
    if(OSMESA_INCLUDE_DIRS AND OSMESA_LIBRARIES)
        set(OSMESA_FOUND TRUE)
    else()
        message(FATAL_ERROR "OSMesa not found. "
            "Install libosmesa6-dev (Debian/Ubuntu) or mesa-libOSMesa-devel (Fedora).")
    endif()
endif()

message(STATUS "OSMesa include : ${OSMESA_INCLUDE_DIRS}")
message(STATUS "OSMesa library : ${OSMESA_LIBRARIES}")

# --- X11 (Linux / BSD) -------------------------------------------------------
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
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${OSMESA_INCLUDE_DIRS}
)

target_link_libraries(glfw_osmesa
    PUBLIC
        ${GLFW_TARGET}
    PRIVATE
        ${OSMESA_LIBRARIES}
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
    target_link_libraries(triangle PRIVATE glfw_osmesa ${OSMESA_LIBRARIES})
    if(UNIX AND NOT APPLE)
        target_link_libraries(triangle PRIVATE m)
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
