// SPDX-License-Identifier: MIT
//
// container.cpp - Implementation of the Layer 1 physical container reader.

#include "rescore/container.hpp"

#include <cstddef>
#include <ios>
#include <ostream>
#include <string>
#include <utility>

namespace rescore::container {
namespace {

/// Render up to `count` bytes of `data` as a classic hex+ASCII dump to `os`.
/// Bounds-safe; prints fewer rows if the buffer is short.
void hex_preview(std::span<const std::byte> data, std::size_t count, std::ostream& os) {
    constexpr std::size_t kPerRow = 16;
    const std::size_t n = count < data.size() ? count : data.size();

    // Save and restore stream formatting flags so we do not leak hex/fill.
    const std::ios::fmtflags saved_flags = os.flags();
    const char saved_fill = os.fill();

    for (std::size_t row = 0; row < n; row += kPerRow) {
        // Offset column.
        os.setf(std::ios::hex, std::ios::basefield);
        os.fill('0');
        os.width(8);
        os << row << "  ";

        // Hex columns.
        for (std::size_t col = 0; col < kPerRow; ++col) {
            const std::size_t i = row + col;
            if (i < n) {
                const auto b = static_cast<unsigned>(static_cast<unsigned char>(data[i]));
                os.setf(std::ios::hex, std::ios::basefield);
                os.fill('0');
                os.width(2);
                os << b << ' ';
            } else {
                os << "   ";
            }
        }

        os << ' ';

        // ASCII column.
        for (std::size_t col = 0; col < kPerRow; ++col) {
            const std::size_t i = row + col;
            if (i < n) {
                const auto c = static_cast<unsigned char>(data[i]);
                const bool printable = c >= 0x20 && c < 0x7F;
                os << (printable ? static_cast<char>(c) : '.');
            }
        }
        os << '\n';
    }

    os.flags(saved_flags);
    os.fill(saved_fill);
}

} // namespace

Result<RawDocument> ContainerReader::parse(std::span<const std::byte> data, Diagnostics& diags) {
    if (data.empty()) {
        diags.error("empty input buffer");
        return Result<RawDocument>::fail(ErrorCode::InvalidArgument, "empty input buffer");
    }

    RawDocument doc;
    doc.version = detect_version(data);

    if (doc.version.era == EnigmaEra::Unknown) {
        diags.error("unrecognized container: no known Enigma Binary File magic found", 0);
        return Result<RawDocument>::fail(
            ErrorCode::BadMagic,
            "unrecognized container: no known Enigma Binary File (.mus) magic found");
    }

    diags.info(std::string("detected container era: ") + to_string(doc.version.era), 0);
    if (!doc.version.version_string.empty()) {
        diags.info("embedded version string: " + doc.version.version_string, 0);
    }

    // parse() returns the detected version; the physical record stream is decoded
    // by the conversion pipeline rather than surfaced here.
    return Result<RawDocument>::ok(std::move(doc));
}

void dump(const RawDocument& doc, std::ostream& os) {
    os << "Rescore container dump\n";
    os << "  era            : " << to_string(doc.version.era) << '\n';
    os << "  version string : "
       << (doc.version.version_string.empty() ? "(none recovered)"
                                              : doc.version.version_string)
       << '\n';
    os << "  byte order     : " << (doc.version.big_endian ? "big-endian" : "little-endian")
       << '\n';
    os << "  records        : " << doc.records.size() << '\n';
}

void dump(const RawDocument& doc, std::span<const std::byte> source, std::ostream& os) {
    dump(doc, os);
    os << "  head preview   :\n";
    constexpr std::size_t kPreviewBytes = 64;
    hex_preview(source, kPreviewBytes, os);
}

} // namespace rescore::container
