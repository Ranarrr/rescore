// tests/version_detect_test.cpp
//
// Unit tests for Layer 1 era detection (rescore::detect_version).
//
// detect_version inspects the documented ASCII magic at the head of a .mus file
// and classifies the container era:
//   v1  (PRONOM fmt/1971): ASCII "Finale" then 0xAA or a TM glyph then " 1"
//   v2  (PRONOM fmt/1972): ASCII "Finale(TM) 2"
//   v3+ (PRONOM fmt/397):  ASCII "ENIGMA BINARY FILE"
// Anything else -> EnigmaEra::Unknown, with no crash on short / empty / random
// input (these are untrusted archival bytes).

#include <rescore/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

using rescore::detect_version;
using rescore::EnigmaEra;

namespace {

// Build a byte buffer from integer literals (each must fit in a byte).
std::vector<std::byte> make_bytes(std::initializer_list<int> vals) {
    std::vector<std::byte> out;
    out.reserve(vals.size());
    for (int v : vals) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

// Bytes of an ASCII string (drops the implicit terminating NUL).
std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return out;
}

// The documented v3+ "ENIGMA BINARY FILE" magic as raw hex, to prove the
// detector matches the on-disk bytes and not just a convenient string spelling.
const std::vector<std::byte> kEnigmaMagic =
    make_bytes({0x45, 0x4E, 0x49, 0x47, 0x4D, 0x41, 0x20, 0x42, 0x49, 0x4E, 0x41,
                0x52, 0x59, 0x20, 0x46, 0x49, 0x4C, 0x45});

} // namespace

TEST_CASE("detect_version: v1 era magic", "[version]") {
    // "Finale" + 0xAA (the 'ª' byte) + " 1".
    std::vector<std::byte> buf = bytes("Finale");
    buf.push_back(std::byte{0xAA});
    const auto tail = bytes(" 1.0 file");
    buf.insert(buf.end(), tail.begin(), tail.end());
    REQUIRE(detect_version(buf).era == EnigmaEra::V1);
}

TEST_CASE("detect_version: v1 era magic with TM glyph variant", "[version]") {
    // Some v1 files carry a TM glyph rather than 0xAA before the " 1".
    std::vector<std::byte> buf = bytes("Finale");
    buf.push_back(std::byte{0x99}); // common single-byte TM glyph in legacy code pages
    const auto tail = bytes(" 1");
    buf.insert(buf.end(), tail.begin(), tail.end());
    REQUIRE(detect_version(buf).era == EnigmaEra::V1);
}

TEST_CASE("detect_version: v2 era magic", "[version]") {
    const auto buf = bytes("Finale(TM) 2 document trailing junk");
    REQUIRE(detect_version(buf).era == EnigmaEra::V2);
}

TEST_CASE("detect_version: v3+ ENIGMA BINARY FILE magic", "[version]") {
    SECTION("exact magic only") {
        REQUIRE(detect_version(kEnigmaMagic).era == EnigmaEra::V3Plus);
    }
    SECTION("magic followed by version string and payload") {
        std::vector<std::byte> buf = kEnigmaMagic;
        const auto tail = bytes(" Finale(R) 2007 ...binary payload...");
        buf.insert(buf.end(), tail.begin(), tail.end());
        REQUIRE(detect_version(buf).era == EnigmaEra::V3Plus);
    }
}

TEST_CASE("detect_version: empty buffer is Unknown, never crashes", "[version]") {
    REQUIRE(detect_version(std::span<const std::byte>{}).era == EnigmaEra::Unknown);
}

TEST_CASE("detect_version: short buffers shorter than any magic", "[version]") {
    // Prefixes of real magics must not be misclassified, and must not overrun.
    REQUIRE(detect_version(bytes("F")).era == EnigmaEra::Unknown);
    REQUIRE(detect_version(bytes("Fin")).era == EnigmaEra::Unknown);
    REQUIRE(detect_version(bytes("ENIGMA")).era == EnigmaEra::Unknown);
    REQUIRE(detect_version(bytes("Finale")).era == EnigmaEra::Unknown);
}

TEST_CASE("detect_version: random junk is Unknown, never crashes", "[version]") {
    const std::vector<std::byte> junk = make_bytes(
        {0x00, 0xFF, 0x7F, 0x80, 0x13, 0x37, 0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD});
    REQUIRE(detect_version(junk).era == EnigmaEra::Unknown);

    // A near-miss that shares a prefix with the ENIGMA magic but diverges.
    // (Named `near_miss`, not `near`: `near` is a reserved keyword on MSVC.)
    auto near_miss = kEnigmaMagic;
    near_miss.back() ^= std::byte{0xFF}; // corrupt the final 'E'
    REQUIRE(detect_version(near_miss).era == EnigmaEra::Unknown);

    // Plausible-looking ASCII that is not any documented Finale magic.
    REQUIRE(detect_version(bytes("MThd")).era == EnigmaEra::Unknown);
    REQUIRE(detect_version(bytes("PK\x03\x04")).era == EnigmaEra::Unknown);
}
