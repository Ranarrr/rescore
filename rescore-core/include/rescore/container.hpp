// SPDX-License-Identifier: MIT
//
// container.hpp - Layer 1, the physical Enigma container reader
// (namespace rescore::container).
//
// Responsibility: turn raw .mus bytes into a flat list of typed RAW records
// (tag + payload) plus the detected version, owning ALL byte-level concerns.
//
// parse() detects the container version/era. The physical record stream is
// surfaced through the conversion pipeline (see convert.hpp); parse() itself
// returns the detected version and an empty record list, and never throws or
// reads out of bounds.

#ifndef RESCORE_CONTAINER_HPP
#define RESCORE_CONTAINER_HPP

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <vector>

#include "rescore/result.hpp"
#include "rescore/version.hpp"

namespace rescore::container {

/// A single decoded physical record from the Enigma object stream. `tag`
/// identifies the record type (Enigma "Others"/"Details"/entry kinds);
/// `payload` is the raw, still-untyped record body; `offset` is the byte
/// position in the source buffer where the record began (for --dump).
struct RawRecord {
    std::uint16_t tag{0};
    std::vector<std::byte> payload;
    std::size_t offset{0};
};

/// The result of physically parsing a .mus buffer: the detected version and the
/// flat record list. When framing is unknown, `records` is empty.
struct RawDocument {
    EnigmaVersion version;
    std::vector<RawRecord> records;
};

/// Stateless front door to Layer 1.
class ContainerReader {
public:
    /// Detect the version. On a buffer whose era cannot be recognized, returns an
    /// error Result. On a recognized buffer, returns a RawDocument with the
    /// version filled in and an empty record list (record decoding is performed
    /// by the conversion pipeline). Never throws, never reads OOB.
    [[nodiscard]] static Result<RawDocument> parse(std::span<const std::byte> data,
                                                   Diagnostics& diags);
};

/// Print a human-readable summary of a RawDocument: era, version string,
/// endianness flag, record count, and a short hex/ASCII preview of the head
/// bytes. Used by the CLI's --dump mode. `head` is sourced from the same buffer
/// the caller parsed (pass the original span via the overload below) - this
/// overload prints only what the RawDocument itself carries.
void dump(const RawDocument& doc, std::ostream& os);

/// As above, but also renders a hex/ASCII preview of the first bytes of the
/// original source buffer (the RawDocument does not retain the raw bytes).
void dump(const RawDocument& doc, std::span<const std::byte> source, std::ostream& os);

} // namespace rescore::container

#endif // RESCORE_CONTAINER_HPP
