# Rescore CLI executable target.
#
# The `rescore` command-line tool is a thin, dependency-free consumer of the
# rescore::core static library. All notation logic lives in the core; this
# target owns only argument parsing, bounded file I/O, and process exit codes.

add_executable(rescore
    src/main.cpp
)

target_link_libraries(rescore
    PRIVATE
        rescore::core
)

# Inherit the project-wide warning configuration and (optionally) sanitizers.
# These helpers are provided by the root cmake/ modules.
rescore_set_warnings(rescore)
rescore_enable_sanitizers(rescore)

# Match the C++ standard used across the project.
target_compile_features(rescore PRIVATE cxx_std_20)

# Install the CLI so downstream packaging can pick it up.
install(TARGETS rescore
    RUNTIME DESTINATION bin
)
