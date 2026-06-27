// SPDX-License-Identifier: MIT
//
// others.hpp - The Enigma "Others" record pool (Layer 1.5).
//
// The legacy .mus "content" chunk holds the document's "Other" records (per the
// Enigma object model): measure specs, staff specs, fonts, text and more. On
// disk it is a single LZSS+Huffman-compressed blob (see lzss.hpp); once
// decompressed it is a flat array of fixed 16-byte records. This module locates
// the chunk, decompresses it, and parses the records into typed values. Every
// read is bounds-checked - untrusted input never faults.
//
// SCOPE: this targets the V3+ Windows (little-endian) layout. Early Macintosh
// (big-endian) files are out of scope here; the reader reports and skips a chunk
// it does not recognize rather than guessing.

#ifndef RESCORE_OTHERS_HPP
#define RESCORE_OTHERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "rescore/result.hpp"

namespace rescore::container {

/// A single Enigma "Other" record: 16 bytes = a comparator (`cmper`), a 2-byte
/// `tag`, and six little-endian 16-bit `data` words. How `data` is interpreted
/// depends on `tag` (e.g. a Measure Spec carries measspace/key/beats/divbeat/...).
struct OtherRecord {
    std::uint16_t cmper{0};
    std::uint16_t tag{0};
    std::array<std::uint16_t, 6> data{};
};

/// Tag of a Measure Spec record ("MS" as a little-endian 16-bit word).
inline constexpr std::uint16_t kTagMeasureSpec = 0x4D53;
/// Tag of a Staff Spec record ("IS" as a little-endian 16-bit word).
inline constexpr std::uint16_t kTagStaffSpec = 0x4953;
/// Smart-shape definition tags. The two ASCII bytes are stored low byte first,
/// so 'xS' (bytes 78 53) reads as 0x5378 and 'xM' (bytes 78 4D) as 0x4D78.
inline constexpr std::uint16_t kTagSmartShapeSeg = 0x5378;    // 'xS' (definition segments)
inline constexpr std::uint16_t kTagSmartShapeMaster = 0x4D78; // 'xM' (segment count)
/// Articulation-definition library tag ('XI', bytes 58 49).
inline constexpr std::uint16_t kTagArticDef = 0x4958;
/// Measure-attached expression assignment tag ('YD', bytes 59 44).
inline constexpr std::uint16_t kTagExprAssign = 0x4459;
/// Text-expression definition tag ('TD', bytes 54 44).
inline constexpr std::uint16_t kTagTextExprDef = 0x4454;

/// On-disk geometry of the content chunk: it begins at offset 0x200 with a
/// 10-byte header `[type:2 LE][size:4 LE][checksum:4 LE]`, where `size` counts
/// the header. The compressed stream is the chunk body that follows.
inline constexpr std::size_t kContentChunkOffset = 0x200;
inline constexpr std::uint16_t kContentChunkType = 15;
inline constexpr std::size_t kContentChunkHeaderSize = 10;
/// A record is sixteen bytes: cmper(2) + tag(2) + six data words(12).
inline constexpr std::size_t kOtherRecordSize = 16;

/// Locate the type-15 content chunk at 0x200, decompress it, and parse the
/// 16-byte "Other" records (little-endian). On a structurally valid buffer
/// returns the records - empty, with a diagnostic, if the chunk is absent or of
/// an unexpected type. Returns an Error only when the buffer cannot even hold the
/// chunk header. Never throws, never reads out of bounds.
[[nodiscard]] Result<std::vector<OtherRecord>> read_others_pool(std::span<const std::byte> mus,
                                                                Diagnostics& diags);

} // namespace rescore::container

#endif // RESCORE_OTHERS_HPP
