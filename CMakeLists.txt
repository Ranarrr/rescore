# Rescore - legacy Finale .mus -> MusicXML 4.0 converter.
# Top-level build configuration. C++20, modern target-based CMake.
#
# This project is an independent parser for the legacy "Enigma Binary File"
# format. It is not affiliated with MakeMusic; Finale is a trademark of its owner.

cmake_minimum_required(VERSION 3.24)

project(
    Rescore
    VERSION 0.1.0
    DESCRIPTION "Convert legacy Finale .mus (Enigma Binary File) into MusicXML 4.0"
    HOMEPAGE_URL "https://github.com/Ranarrr/rescore"
    LANGUAGES CXX)

# ----------------------------------------------------------------------------
# Language standard: C++20, required, no compiler extensions.
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / IDE / clangd tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Detect whether Rescore is the top-level project (vs. add_subdirectory'd).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(
        FATAL_ERROR
            "In-source builds are not allowed. Configure into a separate directory, e.g. "
            "cmake -S . -B build")
endif()

# ----------------------------------------------------------------------------
# Default build type for single-config generators (Make, Ninja).
# Multi-config generators (Visual Studio, Ninja Multi-Config, Xcode) ignore this.
# ----------------------------------------------------------------------------
get_property(_rescore_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _rescore_is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        "Debug"
        CACHE STRING "Build type for single-config generators" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                                                 "RelWithDebInfo" "MinSizeRel")
    message(STATUS "Rescore: CMAKE_BUILD_TYPE not set; defaulting to Debug")
endif()

# Place all build artifacts in predictable per-config locations.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ----------------------------------------------------------------------------
# Project options (see CONVENTIONS).
# ----------------------------------------------------------------------------
option(RESCORE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(RESCORE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(RESCORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# ----------------------------------------------------------------------------
# Shared compiler configuration helpers.
# ----------------------------------------------------------------------------
include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

# ----------------------------------------------------------------------------
# Library and executable targets.
# ----------------------------------------------------------------------------
add_subdirectory(rescore-core)
add_subdirectory(rescore-cli)

# ----------------------------------------------------------------------------
# Installation: export the rescore CLI (and its static core via the export set).
# ----------------------------------------------------------------------------
install(
    TARGETS rescore
    EXPORT RescoreTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/rescore OPTIONAL)
