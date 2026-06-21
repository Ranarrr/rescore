# CompilerWarnings.cmake
#
# Provides rescore_set_warnings(<target>): applies a strict, portable warning set
# to the given target. Warnings are attached PRIVATE so they do not leak to
# consumers of an exported library. When RESCORE_WARNINGS_AS_ERRORS is ON, the
# appropriate "warnings are errors" flag is added.
#
# Usage:
#   include(cmake/CompilerWarnings.cmake)
#   rescore_set_warnings(rescore_core)

include_guard(GLOBAL)

function(rescore_set_warnings target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "rescore_set_warnings: '${target}' is not a target")
    endif()

    set(_rescore_msvc_warnings
        /W4 # baseline high warning level
        /permissive- # standards conformance mode
    )

    set(_rescore_gcc_clang_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion # implicit conversions that may lose data
        -Wshadow # variable declarations shadowing outer scope
        -Wnon-virtual-dtor # base class with virtual fns but non-virtual dtor
        -Woverloaded-virtual # a derived member hides a virtual of the same name
        -Wold-style-cast # C-style casts
        -Wcast-align # casts that increase alignment requirements
        -Wunused # anything unused
        -Wdouble-promotion # float implicitly promoted to double
    )

    # Resolve the active compiler family. Clang-cl reports as MSVC-frontend, so
    # gate on the simulated MSVC frontend rather than just the compiler id.
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_rescore_warnings ${_rescore_msvc_warnings})
        if(RESCORE_WARNINGS_AS_ERRORS)
            list(APPEND _rescore_warnings /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang|IntelLLVM")
        set(_rescore_warnings ${_rescore_gcc_clang_warnings})
        if(RESCORE_WARNINGS_AS_ERRORS)
            list(APPEND _rescore_warnings -Werror)
        endif()
    else()
        message(
            WARNING
                "rescore_set_warnings: unrecognized compiler "
                "'${CMAKE_CXX_COMPILER_ID}'; no warning flags applied to ${target}")
        set(_rescore_warnings "")
    endif()

    target_compile_options(${target} PRIVATE ${_rescore_warnings})
endfunction()
