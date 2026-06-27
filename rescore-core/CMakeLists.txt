# rescore-core - Layer 1 (container), Layer 2 (IR contract), Layer 3 (emitter).
#
# Builds the static library `rescore_core` (alias rescore::core). Public headers
# live under include/rescore/; sources under src/. No third-party dependencies.

add_library(rescore_core STATIC
    src/byte_reader.cpp
    src/ir.cpp
    src/convert.cpp
    src/container/version.cpp
    src/container/container.cpp
    src/container/lzss.cpp
    src/container/others.cpp
    src/container/entries.cpp
    src/musicxml/emit.cpp
)

add_library(rescore::core ALIAS rescore_core)

target_include_directories(rescore_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rescore_core PUBLIC cxx_std_20)

# Shared warning flags and optional sanitizers (defined in cmake/ modules that
# the top-level CMakeLists includes before adding this subdirectory).
rescore_set_warnings(rescore_core)
rescore_enable_sanitizers(rescore_core)
