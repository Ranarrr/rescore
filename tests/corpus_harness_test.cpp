// tests/corpus_harness_test.cpp
//
// Corpus regression harness. Rather than asserting a single fixture, this sweeps
// EVERY .mus file under the corpus directory and pins the converter's contract:
// it must never crash on a committed file, every successful conversion must be
// well-formed MusicXML partwise, and every failure must surface a non-empty
// diagnostic instead of an empty or truncated result. As the corpus grows, new
// files are covered automatically without writing a new test for each one.

#include <rescore/convert.hpp>
#include <rescore/result.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;

namespace {

#ifndef RESCORE_CORPUS_RE_DIR
#define RESCORE_CORPUS_RE_DIR "."
#endif

std::vector<std::byte> slurp(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char c : raw) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return bytes;
}

std::string lower_ext(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

TEST_CASE("corpus: every .mus fixture converts or fails gracefully", "[corpus][regression]") {
    namespace fs = std::filesystem;
    const fs::path dir{RESCORE_CORPUS_RE_DIR};
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        SKIP("corpus directory not available");
    }

    int seen = 0;
    int converted = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || lower_ext(entry.path()) != ".mus") {
            continue;
        }
        ++seen;
        const std::vector<std::byte> bytes = slurp(entry.path());
        rescore::Diagnostics diags;
        // The converter must never throw or crash on any committed fixture.
        const auto res = rescore::convert_mus_to_musicxml(bytes, diags);

        INFO("fixture: " << entry.path().filename().string());
        if (res.has_value()) {
            ++converted;
            // A successful conversion is well-formed MusicXML score-partwise.
            REQUIRE_THAT(res.value(), ContainsSubstring("<score-partwise"));
            REQUIRE_THAT(res.value(), ContainsSubstring("</score-partwise>"));
        } else {
            // A failure must be reported, not silently empty.
            REQUIRE_FALSE(res.message().empty());
        }
    }

    if (seen == 0) {
        SKIP("no .mus fixtures present in the corpus directory");
    }
    // The real committed fixtures must still convert; a regression that breaks
    // every conversion would otherwise slip through as "all failed gracefully".
    REQUIRE(converted > 0);
}
