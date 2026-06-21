# Sanitizers.cmake
#
# Provides rescore_enable_sanitizers(<target>): applies AddressSanitizer and/or
# UndefinedBehaviorSanitizer to the given target according to the cache options
# RESCORE_ENABLE_ASAN / RESCORE_ENABLE_UBSAN.
#
# Sanitizers require the same flags at both compile and link time. They are
# attached PUBLIC so that anything linking an instrumented target is also
# instrumented (mixing instrumented and uninstrumented code is unsupported).
#
# On MSVC this is a no-op with a warning: MSVC has /fsanitize=address but no
# UBSan, and its model differs enough that we do not wire it here. Use the
# Clang/GCC toolchains for sanitizer builds.
#
# Usage:
#   include(cmake/Sanitizers.cmake)
#   rescore_enable_sanitizers(rescore_core)

include_guard(GLOBAL)

function(rescore_enable_sanitizers target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "rescore_enable_sanitizers: '${target}' is not a target")
    endif()

    if(NOT RESCORE_ENABLE_ASAN AND NOT RESCORE_ENABLE_UBSAN)
        return()
    endif()

    # MSVC frontend (cl or clang-cl): not supported by this module.
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(
            WARNING
                "rescore_enable_sanitizers: ASan/UBSan are not wired for the MSVC "
                "frontend; skipping instrumentation for ${target}. Use a Clang or "
                "GCC toolchain for sanitizer builds.")
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang|IntelLLVM")
        message(
            WARNING
                "rescore_enable_sanitizers: unrecognized compiler "
                "'${CMAKE_CXX_COMPILER_ID}'; skipping instrumentation for ${target}.")
        return()
    endif()

    set(_rescore_sanitizers "")
    if(RESCORE_ENABLE_ASAN)
        list(APPEND _rescore_sanitizers "address")
    endif()
    if(RESCORE_ENABLE_UBSAN)
        list(APPEND _rescore_sanitizers "undefined")
    endif()

    list(JOIN _rescore_sanitizers "," _rescore_sanitizer_arg)
    message(STATUS "rescore: enabling sanitizers (${_rescore_sanitizer_arg}) for ${target}")

    set(_rescore_sanitizer_flags -fsanitize=${_rescore_sanitizer_arg}
                                 -fno-omit-frame-pointer)

    target_compile_options(${target} PUBLIC ${_rescore_sanitizer_flags})
    target_link_options(${target} PUBLIC ${_rescore_sanitizer_flags})
endfunction()
