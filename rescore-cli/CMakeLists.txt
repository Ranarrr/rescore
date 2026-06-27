# Rescore CLI executable target (rescore-cli).
#
# The command-line tool is a thin, dependency-free consumer of the rescore::core
# static library. All notation logic lives in the core; this target owns only
# argument parsing, bounded file I/O, and process exit codes. The binary is named
# `rescore-cli`; the plain `rescore` name belongs to the GUI app.

add_executable(rescore_cli
    src/main.cpp
)

target_link_libraries(rescore_cli
    PRIVATE
        rescore::core
)

set_target_properties(rescore_cli PROPERTIES OUTPUT_NAME rescore-cli)

# Inherit the project-wide warning configuration and (optionally) sanitizers.
# These helpers are provided by the root cmake/ modules.
rescore_set_warnings(rescore_cli)
rescore_enable_sanitizers(rescore_cli)

# Match the C++ standard used across the project.
target_compile_features(rescore_cli PRIVATE cxx_std_20)

# Install the CLI so downstream packaging can pick it up.
install(TARGETS rescore_cli
    RUNTIME DESTINATION bin
)
