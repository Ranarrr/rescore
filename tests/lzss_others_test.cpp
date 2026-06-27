// tests/lzss_others_test.cpp
//
// Tests for the content-region decompressor (rescore::lzss), the Enigma
// "Others" pool reader (rescore::container::read_others_pool), the IR attribute
// mappings, and the end-to-end convert path. The decompression/parse cases read
// committed .mus fixtures from tests/corpus/re (path injected via the
// RESCORE_CORPUS_RE_DIR compile definition); they SKIP, rather than fail, if a
// fixture is unavailable so the suite still runs in a stripped checkout.

#include <rescore/convert.hpp>
#include <rescore/lzss.hpp>
#include <rescore/others.hpp>
#include <rescore/result.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using rescore::container::OtherRecord;
using rescore::container::kTagMeasureSpec;
using rescore::container::kTagStaffSpec;

namespace {

#ifndef RESCORE_CORPUS_RE_DIR
#define RESCORE_CORPUS_RE_DIR "."
#endif

/// Load a fixture from the RE corpus, or std::nullopt if it is not present.
std::optional<std::vector<std::byte>> load_fixture(const std::string& name) {
    const std::string path = std::string(RESCORE_CORPUS_RE_DIR) + "/" + name;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char c : raw) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return bytes;
}

/// Find the `nth` (1-based) record with the given tag and cmper, or nullptr.
const OtherRecord* find_record(const std::vector<OtherRecord>& records, std::uint16_t tag,
                               std::uint16_t cmper, int nth = 1) {
    int seen = 0;
    for (const auto& record : records) {
        if (record.tag == tag && record.cmper == cmper) {
            if (++seen == nth) {
                return &record;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("lzss: baseline content chunk decompresses to the expected size", "[lzss]") {
    const auto buf = load_fixture("empty-cmaj-baseline.mus");
    if (!buf) {
        SKIP("fixture empty-cmaj-baseline.mus not available");
    }
    const std::size_t body = rescore::container::kContentChunkOffset +
                             rescore::container::kContentChunkHeaderSize; // 0x20A
    REQUIRE(buf->size() > body);
    const std::span<const std::byte> stream(buf->data() + body, buf->size() - body);

    rescore::Diagnostics diags;
    const auto res = rescore::lzss::inflate_content(stream, diags);
    REQUIRE(res.has_value());
    // Deterministic for this committed fixture (verified byte-for-byte vs Finale).
    CHECK(res.value().size() == 84560);
}

TEST_CASE("others: baseline 4/4 C-major measure spec + treble clef", "[others][lzss]") {
    const auto buf = load_fixture("empty-cmaj-baseline.mus");
    if (!buf) {
        SKIP("fixture empty-cmaj-baseline.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::container::read_others_pool(*buf, diags);
    REQUIRE(res.has_value());
    const auto& records = res.value();
    REQUIRE(records.size() > 1000);

    // Measure spec, measure 1, incidence 1: [measspace, key, beats, divbeat, ...].
    const OtherRecord* ms = find_record(records, kTagMeasureSpec, /*cmper=*/1, /*nth=*/1);
    REQUIRE(ms != nullptr);
    CHECK(ms->data[2] == 4);    // beats
    CHECK(ms->data[3] == 1024); // divbeat (EDU per beat -> quarter)
    CHECK(ms->data[1] == 0);    // key field (C major, fifths 0)

    // Staff spec, staff 1, 2nd incidence: clef index in data[0] (0 = treble).
    const OtherRecord* is = find_record(records, kTagStaffSpec, /*cmper=*/1, /*nth=*/2);
    REQUIRE(is != nullptr);
    CHECK(is->data[0] == 0);
}

TEST_CASE("others: bass fixture reports clef index 3", "[others][lzss]") {
    const auto buf = load_fixture("empty-bass.mus");
    if (!buf) {
        SKIP("fixture empty-bass.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::container::read_others_pool(*buf, diags);
    REQUIRE(res.has_value());
    const OtherRecord* is = find_record(res.value(), kTagStaffSpec, /*cmper=*/1, /*nth=*/2);
    REQUIRE(is != nullptr);
    CHECK(is->data[0] == 3); // bass
}

TEST_CASE("others: key field tracks the key signature (gmaj/fmaj)", "[others][lzss]") {
    if (const auto g = load_fixture("empty-gmaj.mus")) {
        rescore::Diagnostics diags;
        const auto res = rescore::container::read_others_pool(*g, diags);
        REQUIRE(res.has_value());
        const OtherRecord* ms = find_record(res.value(), kTagMeasureSpec, 1, 1);
        REQUIRE(ms != nullptr);
        CHECK(ms->data[1] == 1); // G major: 1 sharp
        CHECK(rescore::key_from_field(ms->data[1]).fifths == 1);
    }
    if (const auto f = load_fixture("empty-fmaj.mus")) {
        rescore::Diagnostics diags;
        const auto res = rescore::container::read_others_pool(*f, diags);
        REQUIRE(res.has_value());
        const OtherRecord* ms = find_record(res.value(), kTagMeasureSpec, 1, 1);
        REQUIRE(ms != nullptr);
        CHECK(ms->data[1] == 0x00FF); // F major: 1 flat
        CHECK(rescore::key_from_field(ms->data[1]).fifths == -1);
    }
}

TEST_CASE("convert: baseline yields MusicXML with 4/4 treble C-major attributes", "[convert]") {
    const auto buf = load_fixture("empty-cmaj-baseline.mus");
    if (!buf) {
        SKIP("fixture empty-cmaj-baseline.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<beats>4</beats>"));
    REQUIRE_THAT(xml, ContainsSubstring("<beat-type>4</beat-type>"));
    REQUIRE_THAT(xml, ContainsSubstring("<sign>G</sign>"));
    REQUIRE_THAT(xml, ContainsSubstring("<line>2</line>"));
    REQUIRE_THAT(xml, ContainsSubstring("<fifths>0</fifths>"));
}

TEST_CASE("convert: bass fixture yields an F-clef on line 4", "[convert]") {
    const auto buf = load_fixture("empty-bass.mus");
    if (!buf) {
        SKIP("fixture empty-bass.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<sign>F</sign>"));
    REQUIRE_THAT(xml, ContainsSubstring("<line>4</line>"));
}

TEST_CASE("derive_time_signature: simple and compound meters", "[convert][time]") {
    using rescore::derive_time_signature;
    // Simple meters: divbeat is an undotted note value.
    CHECK(derive_time_signature(4, 1024).beats == 4);
    CHECK(derive_time_signature(4, 1024).beat_type == 4); // 4/4
    CHECK(derive_time_signature(3, 1024).beats == 3);
    CHECK(derive_time_signature(3, 1024).beat_type == 4); // 3/4
    CHECK(derive_time_signature(2, 2048).beat_type == 2); // 2/2 (half-note beats)
    // Compound meters: divbeat is a dotted note value -> 3 subdivisions/beat.
    CHECK(derive_time_signature(2, 1536).beats == 6);
    CHECK(derive_time_signature(2, 1536).beat_type == 8); // 6/8
    CHECK(derive_time_signature(3, 1536).beats == 9);
    CHECK(derive_time_signature(3, 1536).beat_type == 8); // 9/8
    // Degenerate input falls back to 4/4 rather than dividing by zero.
    CHECK(derive_time_signature(0, 0).beats == 4);
    CHECK(derive_time_signature(0, 0).beat_type == 4);
}

TEST_CASE("clef_from_index: verified and conventional mappings", "[convert][clef]") {
    using rescore::clef_from_index;
    CHECK(clef_from_index(0).sign == rescore::ir::ClefSign::G);
    CHECK(clef_from_index(0).line == 2);
    CHECK(clef_from_index(3).sign == rescore::ir::ClefSign::F);
    CHECK(clef_from_index(3).line == 4);
    // Unknown indices fall back to treble, never crash.
    CHECK(clef_from_index(99).sign == rescore::ir::ClefSign::G);
}

TEST_CASE("key_from_field: signed fifths and mode bank", "[convert][key]") {
    using rescore::key_from_field;
    CHECK(key_from_field(0x0000).fifths == 0);
    CHECK(key_from_field(0x0001).fifths == 1);
    CHECK(key_from_field(0x00FF).fifths == -1); // low byte is signed
    CHECK(key_from_field(0x0000).mode == rescore::ir::KeySignature::Mode::Major);
    CHECK(key_from_field(0x0100).mode == rescore::ir::KeySignature::Mode::Minor); // bank 1
}
