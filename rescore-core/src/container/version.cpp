// SPDX-License-Identifier: MIT
//
// version.cpp - Implementation of Enigma Binary File era detection.
//
// All matching is done over a borrowed span via ByteReader-style bounds-safe
// access (here, simple indexed comparisons guarded by length checks). No read
// touches memory outside `data`.

#include "rescore/version.hpp"

#include <array>
#include <cctype>
#include <cstdint>

namespace rescore {
namespace {

// "ENIGMA BINARY FILE" - the V3+ magic (PRONOM fmt/397).
constexpr std::array<unsigned char, 18> kEnigmaMagic = {
    'E', 'N', 'I', 'G', 'M', 'A', ' ', 'B', 'I', 'N', 'A', 'R', 'Y', ' ', 'F', 'I', 'L', 'E'};

// "Finale" - shared prefix of the V1 and V2 magics.
constexpr std::array<unsigned char, 6> kFinalePrefix = {'F', 'i', 'n', 'a', 'l', 'e'};

// "Finale(TM) 2" - the V2 magic (PRONOM fmt/1972).
constexpr std::array<unsigned char, 12> kFinaleV2Magic = {
    'F', 'i', 'n', 'a', 'l', 'e', '(', 'T', 'M', ')', ' ', '2'};

[[nodiscard]] unsigned char byte_at(std::span<const std::byte> data, std::size_t i) {
    return static_cast<unsigned char>(data[i]);
}

/// True if `needle` occurs at byte offset `at` in `data` (bounds-checked).
template <std::size_t N>
[[nodiscard]] bool matches_at(std::span<const std::byte> data, std::size_t at,
                              const std::array<unsigned char, N>& needle) {
    if (at + N > data.size()) {
        return false;
    }
    for (std::size_t i = 0; i < N; ++i) {
        if (byte_at(data, at + i) != needle[i]) {
            return false;
        }
    }
    return true;
}

/// Search the first `limit` bytes of `data` for `needle`; returns its start
/// offset or npos. Bounds-safe.
template <std::size_t N>
[[nodiscard]] std::size_t find(std::span<const std::byte> data,
                               const std::array<unsigned char, N>& needle, std::size_t limit) {
    static_assert(N > 0, "needle must be non-empty");
    const std::size_t cap = limit < data.size() ? limit : data.size();
    if (cap < N) {
        return static_cast<std::size_t>(-1);
    }
    for (std::size_t at = 0; at + N <= cap; ++at) {
        if (matches_at(data, at, needle)) {
            return at;
        }
    }
    return static_cast<std::size_t>(-1);
}

/// Heuristically recover a printable version label starting at `from`. Collects
/// printable ASCII (and the common 0xAA/TM trademark glyphs, normalized away)
/// until a control byte or a run-length cap. Trims trailing whitespace.
[[nodiscard]] std::string recover_version_string(std::span<const std::byte> data,
                                                 std::size_t from) {
    constexpr std::size_t kMaxLabel = 64;
    std::string out;
    for (std::size_t i = from; i < data.size() && out.size() < kMaxLabel; ++i) {
        const unsigned char c = byte_at(data, i);
        const bool printable = c >= 0x20 && c < 0x7F;
        if (printable) {
            out.push_back(static_cast<char>(c));
        } else if (c == 0xAA || c == 0x99) {
            // 0xAA (Mac feminine ordinal / TM-ish) and 0x99 (CP1252 TM): skip
            // the trademark glyph rather than embedding a non-ASCII byte.
            continue;
        } else {
            break;
        }
    }
    // Trim trailing whitespace.
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) {
        out.pop_back();
    }
    return out;
}

} // namespace

const char* to_string(EnigmaEra era) noexcept {
    switch (era) {
    case EnigmaEra::V1:
        return "V1 (fmt/1971)";
    case EnigmaEra::V2:
        return "V2 (fmt/1972)";
    case EnigmaEra::V3Plus:
        return "V3+ (fmt/397, ENIGMA BINARY FILE)";
    case EnigmaEra::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

EnigmaVersion detect_version(std::span<const std::byte> data) {
    EnigmaVersion result; // defaults: Unknown, "", big_endian=false

    // The magic for any era lives in the first part of the file. Cap the scan
    // so a huge unrelated buffer cannot make detection quadratic-expensive.
    constexpr std::size_t kScanLimit = 256;

    // --- V3+ : "ENIGMA BINARY FILE" -----------------------------------------
    if (const std::size_t e = find(data, kEnigmaMagic, kScanLimit);
        e != static_cast<std::size_t>(-1)) {
        result.era = EnigmaEra::V3Plus;
        // The build label typically follows the magic; recover from just past it.
        result.version_string = recover_version_string(data, e + kEnigmaMagic.size());
        if (result.version_string.empty()) {
            result.version_string = "ENIGMA BINARY FILE";
        }
        return result;
    }

    // --- V2 : "Finale(TM) 2" ------------------------------------------------
    if (const std::size_t v2 = find(data, kFinaleV2Magic, kScanLimit);
        v2 != static_cast<std::size_t>(-1)) {
        result.era = EnigmaEra::V2;
        result.version_string = recover_version_string(data, v2);
        return result;
    }

    // --- V1 : "Finale" + 0xAA/TM glyph + " 1" -------------------------------
    // Match the "Finale" prefix, then require the documented trademark glyph
    // followed (allowing a separating space) by an ASCII '1'.
    if (const std::size_t f = find(data, kFinalePrefix, kScanLimit);
        f != static_cast<std::size_t>(-1)) {
        const std::size_t after = f + kFinalePrefix.size();
        // Look at the few bytes following "Finale" for the TM glyph + '1'.
        bool saw_tm = false;
        bool saw_one = false;
        for (std::size_t i = after; i < data.size() && i < after + 6; ++i) {
            const unsigned char c = byte_at(data, i);
            if (c == 0xAA || c == 0x99 || c == 0x84 /* CP1252 quote-ish TM */) {
                saw_tm = true;
            } else if (c == '1') {
                saw_one = true;
                break;
            }
        }
        if (saw_tm && saw_one) {
            result.era = EnigmaEra::V1;
            result.version_string = recover_version_string(data, f);
            return result;
        }
    }

    return result; // Unknown.
}

} // namespace rescore
