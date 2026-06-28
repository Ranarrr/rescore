# tests/CMakeLists.txt
#
# Catch2 v3 test suite for Rescore. Tests are pure-IR / pure-byte / pure-emitter
# checks: no .mus binary fixtures and no network are required to run the unit
# suite. The corpus differential tests (see tests/corpus/) are layered on later.
#
# Catch2 is pulled via FetchContent so a fresh checkout builds with no system
# packages, but we first honour an already-installed Catch2 (faster CI, distro
# packaging) by trying find_package() and only fetching on a miss.

include(FetchContent)

# Prefer a system / already-provided Catch2 if present; otherwise fetch a pinned
# release. CONFIG mode avoids accidentally matching an unrelated FindCatch2.
find_package(Catch2 3 QUIET CONFIG)

if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found locally; fetching v3.5.4 via FetchContent")
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    # Catch2's CMake helpers (catch_discover_tests) live in its extras dir.
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()

# Catch2's CTest integration. When Catch2 came from find_package, the helper is
# installed alongside it and Catch2::Catch2 brings the module path in; when it
# came from FetchContent we appended the extras dir above. Guard the include so
# a packaging layout that already provided it does not error on re-include.
include(Catch)

# Shield the project's strict warnings-as-errors posture (/W4 /WX, -Werror) from
# third-party Catch2 header warnings by treating Catch2's headers as system
# headers. find_package() imported targets are already system; this matters for
# the FetchContent source build. (SYSTEM target property requires CMake >= 3.25;
# CI provisions a newer CMake. On older CMake the property is simply ignored.)
foreach(_rescore_catch2_target Catch2 Catch2WithMain)
    if(TARGET ${_rescore_catch2_target})
        set_target_properties(${_rescore_catch2_target} PROPERTIES SYSTEM ON)
    endif()
endforeach()

add_executable(rescore_tests
    byte_reader_test.cpp
    version_detect_test.cpp
    ir_duration_test.cpp
    emit_trivial_test.cpp
    lzss_others_test.cpp
    entries_test.cpp
    corpus_harness_test.cpp
)

target_link_libraries(rescore_tests
    PRIVATE
        rescore::core
        Catch2::Catch2WithMain
)

# The decompressor/Others tests read committed .mus fixtures. Inject the corpus
# directory as a string so the test binary finds them regardless of the working
# directory; the tests SKIP if a fixture is absent (e.g. a stripped checkout).
target_compile_definitions(rescore_tests
    PRIVATE
        RESCORE_CORPUS_RE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/re")

# Apply the same warning/sanitizer posture as the rest of the project so the
# test binary is itself an ASan/UBSan probe of the core library.
if(COMMAND rescore_enable_sanitizers)
    rescore_enable_sanitizers(rescore_tests)
endif()
if(COMMAND rescore_set_warnings)
    rescore_set_warnings(rescore_tests)
endif()

# Register each TEST_CASE as an individual CTest test.
catch_discover_tests(rescore_tests)
