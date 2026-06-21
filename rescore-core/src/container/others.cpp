// SPDX-License-Identifier: MIT
//
// others.cpp - Implementation of the Enigma "Others" record pool reader.

#include "rescore/others.hpp"

#include "rescore/byte_reader.hpp"
#include "rescore/lzss.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace rescore::container {

Result<std::vector<OtherRecord>> read_others_pool(std::span<const std::byte> mus,
                                                  Diagnostics& diags) {
    // --- locate and validate the content chunk header at 0x200 ---------------
    ByteReader header(mus);
    if (!header.seek(kContentChunkOffset)) {
        return Result<std::vector<OtherRecord>>::fail(
            ErrorCode::UnexpectedEof, "buffer too small to contain the content chunk header");
    }
    const std::optional<std::uint16_t> type = header.read_u16le();
    const std::optional<std::uint32_t> size = header.read_u32le();
    if (!type || !size) {
        return Result<std::vector<OtherRecord>>::fail(ErrorCode::UnexpectedEof,
                                                      "truncated content chunk header");
    }

    if (*type != kContentChunkType) {
        diags.warn("content chunk at 0x200 has unexpected type " + std::to_string(*type) +
                       " (expected " + std::to_string(kContentChunkType) +
                       "); no Other records decoded",
                   kContentChunkOffset);
        return Result<std::vector<OtherRecord>>::ok(std::vector<OtherRecord>{});
    }
    if (*size < kContentChunkHeaderSize) {
        diags.warn("content chunk size is smaller than its header; no Other records decoded",
                   kContentChunkOffset);
        return Result<std::vector<OtherRecord>>::ok(std::vector<OtherRecord>{});
    }

    // The compressed stream is the chunk body [off+10, off+size), clamped to the
    // buffer (a size field that overruns the file is tolerated, not trusted).
    const std::size_t data_off = kContentChunkOffset + kContentChunkHeaderSize;
    // Clamp the chunk end to the buffer WITHOUT ever forming the wrapping sum
    // kContentChunkOffset + *size (*size is attacker-controlled; on a 32-bit
    // size_t the raw addition could overflow).
    const auto size_bytes = static_cast<std::size_t>(*size);
    std::size_t data_end = mus.size();
    if (size_bytes <= mus.size() && kContentChunkOffset <= mus.size() - size_bytes) {
        data_end = kContentChunkOffset + size_bytes;
    }
    if (data_off >= data_end) {
        diags.warn("content chunk has no compressed body; no Other records decoded",
                   kContentChunkOffset);
        return Result<std::vector<OtherRecord>>::ok(std::vector<OtherRecord>{});
    }
    const std::span<const std::byte> stream = mus.subspan(data_off, data_end - data_off);

    // --- decompress ----------------------------------------------------------
    Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
    if (!inflated) {
        return Result<std::vector<OtherRecord>>::fail(inflated.code(), inflated.message());
    }
    const std::vector<std::byte>& bytes = inflated.value();

    // --- parse fixed 16-byte little-endian records ---------------------------
    const auto u16 = [&bytes](std::size_t at) -> std::uint16_t {
        const auto lo = std::to_integer<unsigned>(bytes[at]);
        const auto hi = std::to_integer<unsigned>(bytes[at + 1]);
        return static_cast<std::uint16_t>(lo | (hi << 8));
    };

    std::vector<OtherRecord> records;
    records.reserve(bytes.size() / kOtherRecordSize);
    for (std::size_t off = 0; off + kOtherRecordSize <= bytes.size(); off += kOtherRecordSize) {
        OtherRecord record;
        record.cmper = u16(off);
        record.tag = u16(off + 2);
        for (std::size_t k = 0; k < record.data.size(); ++k) {
            record.data[k] = u16(off + 4 + 2 * k);
        }
        records.push_back(record);
    }

    diags.info("decoded " + std::to_string(records.size()) +
               " Enigma Other records from the content pool");
    return Result<std::vector<OtherRecord>>::ok(std::move(records));
}

} // namespace rescore::container
