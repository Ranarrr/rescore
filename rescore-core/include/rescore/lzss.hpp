// SPDX-License-Identifier: MIT
//
// lzss.hpp - Decompressor for the legacy Finale .mus content region.
//
// The "content" chunk (the per-document Enigma "Others" pool: measure specs,
// staff specs, fonts, text, ...) is stored compressed with a custom
// LZSS + static-Huffman scheme. This module decompresses it.
//
// Like the rest of the parse path it is exception-free and bounds-safe on
// untrusted archival input: it never reads out of bounds, caps total output to
// guard against decompression bombs, and validates every back-reference before
// copying.

#ifndef RESCORE_LZSS_HPP
#define RESCORE_LZSS_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "rescore/result.hpp"

namespace rescore::lzss {

/// Upper bound on decompressed output (decompression-bomb guard). The largest
/// real corpus content chunk so far is ~85 KB; 64 MiB is a generous ceiling.
inline constexpr std::size_t kDefaultMaxOutput = 64u * 1024u * 1024u;

/// Decompress an Enigma content stream. `stream` must begin at the 3-byte stream
/// header `[method][nbits][initbits]`. Both stream methods are supported: method 0
/// (raw 8-bit literals) and method 1 (literals Huffman-coded with a static table).
///
/// Returns the decompressed bytes. A truncated or internally inconsistent stream
/// yields the bytes decoded so far plus a Warning diagnostic (best-effort
/// recovery) rather than an error - it never crashes, loops unboundedly, or
/// reads out of bounds. An unusable header returns an Error.
[[nodiscard]] Result<std::vector<std::byte>> inflate_content(
    std::span<const std::byte> stream, Diagnostics& diags,
    std::size_t max_output = kDefaultMaxOutput);

} // namespace rescore::lzss

#endif // RESCORE_LZSS_HPP
