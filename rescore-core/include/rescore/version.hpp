// SPDX-License-Identifier: MIT
//
// version.hpp - Enigma Binary File era detection from the file header.
//
// This is REAL, working code: it matches the documented PRONOM/LOC magic
// signatures and recovers the embedded ASCII version string so that downstream
// layers can version-gate record parsing. Record layouts drift across eras, so
// every later decision keys off the EnigmaVersion produced here.

#ifndef RESCORE_VERSION_HPP
#define RESCORE_VERSION_HPP

#include <cstddef>
#include <span>
#include <string>

namespace rescore {

/// The three documented signature eras of the legacy .mus container, plus a
/// catch-all for buffers that match none of them.
///
///   V1     : PRONOM fmt/1971 - "Finale" + 0xAA/TM glyph + " 1"
///   V2     : PRONOM fmt/1972 - "Finale(TM) 2"
///   V3Plus : PRONOM fmt/397  - "ENIGMA BINARY FILE" (Finale 3.0 .. 2012)
enum class EnigmaEra { V1, V2, V3Plus, Unknown };

/// Result of header detection. `version_string` is a best-effort recovery of
/// the human-readable build label embedded near the magic (e.g. "Finale(R)
/// 2007"); it may be empty if none was found. `big_endian` defaults to false
/// (Windows/little-endian); early Mac files are big-endian - see TODO below.
struct EnigmaVersion {
    EnigmaEra era{EnigmaEra::Unknown};
    std::string version_string;
    bool big_endian{false};
};

/// Human-readable name for an era (for --dump and logging).
[[nodiscard]] const char* to_string(EnigmaEra era) noexcept;

/// Inspect the head of a .mus buffer and classify its container era, recovering
/// the embedded version string where possible. Never throws; on no match
/// returns {EnigmaEra::Unknown, "", false}.
///
/// TODO(endianness): big_endian is currently always false. Early Macintosh
/// Finale files are big-endian. Distinguishing Mac vs Windows requires sample
/// files we do not yet have; once available, set big_endian from a header
/// discriminator here rather than guessing downstream.
[[nodiscard]] EnigmaVersion detect_version(std::span<const std::byte> data);

} // namespace rescore

#endif // RESCORE_VERSION_HPP
