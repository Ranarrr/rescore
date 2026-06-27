// SPDX-License-Identifier: MIT
//
// lzss.cpp - Implementation of the content-region decompressor.
//
// The codec is a custom LZSS + static-Huffman scheme:
//   * LSB-first bit reader; one input byte is refilled at a time.
//   * Per token: one flag bit. flag 0 -> literal (raw 8 bits). flag 1 -> a match.
//   * Match length: take 8 bits, look up a 16-entry symbol Huffman table -> idx;
//     consume idx's code length; read extra[idx] extra bits;
//     value = base[idx] + extra; symbol = value + 256; length = symbol - 254.
//     A symbol >= 773 is the end-of-stream marker.
//   * Match distance: take 8 bits, look up a 64-entry distance Huffman table ->
//     idx; consume idx's code length; read `nbits` extra bits (2 when length==2);
//     distance = (idx << nbits) | extra, then +1.
//   * Plain sliding-window LZ77 copy.

#include "rescore/lzss.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace rescore::lzss {
namespace {

// --- static Huffman tables --------------------------------------------------
constexpr std::array<std::uint8_t, 16> kSymBits = {3, 2, 3, 3, 4, 4, 4, 5,
                                                   5, 5, 5, 6, 6, 6, 7, 7};
constexpr std::array<std::uint8_t, 16> kSymCodes = {0x05, 0x03, 0x01, 0x06, 0x0a, 0x02,
                                                    0x0c, 0x14, 0x04, 0x18, 0x08, 0x30,
                                                    0x10, 0x20, 0x40, 0x00};
constexpr std::array<std::uint8_t, 16> kSymExtra = {0, 0, 0, 0, 0, 0, 0, 0,
                                                    1, 2, 3, 4, 5, 6, 7, 8};
constexpr std::array<std::uint16_t, 16> kSymBase = {0,  1,  2,  3,  4,   5,   6,   7,
                                                    8, 10, 14, 22, 38, 70, 134, 262};

constexpr std::array<std::uint8_t, 64> kDistBits = {
    2, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
constexpr std::array<std::uint8_t, 64> kDistCodes = {
    0x03, 0x0d, 0x05, 0x19, 0x09, 0x11, 0x01, 0x3e, 0x1e, 0x2e, 0x0e, 0x36, 0x16,
    0x26, 0x06, 0x3a, 0x1a, 0x2a, 0x0a, 0x32, 0x12, 0x22, 0x42, 0x02, 0x7c, 0x3c,
    0x5c, 0x1c, 0x6c, 0x2c, 0x4c, 0x0c, 0x74, 0x34, 0x54, 0x14, 0x64, 0x24, 0x44,
    0x04, 0x78, 0x38, 0x58, 0x18, 0x68, 0x28, 0x48, 0x08, 0xf0, 0x70, 0xb0, 0x30,
    0xd0, 0x50, 0x90, 0x10, 0xe0, 0x60, 0xa0, 0x20, 0xc0, 0x40, 0x80, 0x00};

// Build the 256-entry LSB-first code -> symbol lookup: for each symbol the
// (bit-reversed canonical) code is the low `bits` of an
// 8-bit window, so every 8-bit window value selects exactly one symbol.
template <std::size_t N>
constexpr std::array<std::uint8_t, 256> build_lookup(const std::array<std::uint8_t, N>& bits,
                                                     const std::array<std::uint8_t, N>& codes) {
    std::array<std::uint8_t, 256> table{};
    for (std::size_t k = N; k-- > 0;) {
        const unsigned step = 1u << bits[k];
        for (unsigned j = codes[k]; j < 256u; j += step) {
            table[j] = static_cast<std::uint8_t>(k);
        }
    }
    return table;
}

constexpr std::array<std::uint8_t, 256> kSymLookup = build_lookup(kSymBits, kSymCodes);
constexpr std::array<std::uint8_t, 256> kDistLookup = build_lookup(kDistBits, kDistCodes);

// Method-1 streams Huffman-code their literals with a static 256-symbol table
// (the match/distance codecs are identical to method 0). The two tables below are
// the per-symbol code length and the LSB-first canonical code.
#include "lzss_lit_tables.inc" // defines kLitLen[256], kLitCode[256]

constexpr int kLitMaxLen = 13; // = max(kLitLen)

/// Sentinel-keyed (length, code) -> symbol table for the method-1 literal codes.
/// The index is (1 << len) | code, so an LSB-first prefix resolves to its symbol
/// at exactly the right length. Built once on first use; -1 = no symbol.
const std::array<std::int16_t, 1u << (kLitMaxLen + 1)>& lit_decode_table() {
    static const std::array<std::int16_t, 1u << (kLitMaxLen + 1)> table = []() {
        std::array<std::int16_t, 1u << (kLitMaxLen + 1)> t{};
        t.fill(static_cast<std::int16_t>(-1));
        for (std::size_t sym = 0; sym < kLitLen.size(); ++sym) {
            const unsigned len = kLitLen[sym];
            const unsigned code = kLitCode[sym] & ((1u << len) - 1u);
            t[(static_cast<std::size_t>(1) << len) | code] = static_cast<std::int16_t>(sym);
        }
        return t;
    }();
    return table;
}

/// LSB-first bit reader over a borrowed byte span. Bits are
/// consumed from the low end of the buffer; the buffer is topped up one input
/// byte at a time. On input exhaustion `failed_` latches and no byte is read
/// past the end of the span.
class BitReader {
public:
    BitReader(std::span<const std::byte> data, std::size_t pos, std::uint32_t init) noexcept
        : data_(data), pos_(pos), buf_(init) {}

    /// The current bit buffer; the caller masks off the low bits it needs before
    /// consuming them.
    [[nodiscard]] std::uint32_t bits() const noexcept { return buf_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

    void consume(int n) noexcept {
        if (failed_) {
            return;
        }
        if (count_ >= n) {
            count_ -= n;
            buf_ >>= n;
            return;
        }
        buf_ >>= count_;
        if (pos_ >= data_.size()) {
            failed_ = true;
            return;
        }
        const auto next = static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_]));
        ++pos_;
        buf_ = (buf_ | (next << 8)) >> (n - count_);
        count_ = count_ - n + 8;
    }

private:
    std::span<const std::byte> data_;
    std::size_t pos_{0};
    std::uint32_t buf_{0};
    int count_{0};
    bool failed_{false};
};

} // namespace

Result<std::vector<std::byte>> inflate_content(std::span<const std::byte> stream,
                                               Diagnostics& diags, std::size_t max_output) {
    if (stream.size() < 3) {
        return Result<std::vector<std::byte>>::fail(
            ErrorCode::UnexpectedEof, "content stream too short for its 3-byte header");
    }
    const auto method = static_cast<unsigned>(static_cast<std::uint8_t>(stream[0]));
    const int nbits = static_cast<int>(static_cast<std::uint8_t>(stream[1]));
    const auto init = static_cast<std::uint32_t>(static_cast<std::uint8_t>(stream[2]));

    if (method != 0 && method != 1) {
        return Result<std::vector<std::byte>>::fail(
            ErrorCode::NotImplemented,
            "content stream uses an unsupported compression method (" + std::to_string(method) +
                ")");
    }
    if (nbits < 4 || nbits > 6) {
        return Result<std::vector<std::byte>>::fail(
            ErrorCode::MalformedData,
            "content stream header has out-of-range nbits=" + std::to_string(nbits));
    }
    const std::uint32_t extra_mask = 0xFFFFu >> (16 - nbits);

    BitReader br(stream, 3, init);
    std::vector<std::byte> out;

    bool ended = false;
    while (out.size() < max_output) {
        const std::uint32_t flag = br.bits() & 1u;
        br.consume(1);
        if (br.failed()) {
            break;
        }

        if (flag == 0u) { // literal
            std::uint8_t literal = 0;
            if (method == 1u) {
                // Method-1 literals are Huffman-coded: accumulate the LSB-first
                // prefix one bit at a time until it resolves to a symbol.
                const std::array<std::int16_t, 1u << (kLitMaxLen + 1)>& dec = lit_decode_table();
                std::uint32_t acc = 0;
                int len = 0;
                int sym = -1;
                while (len < kLitMaxLen) {
                    const std::uint32_t bit = br.bits() & 1u;
                    br.consume(1);
                    if (br.failed()) {
                        break;
                    }
                    acc |= bit << len;
                    ++len;
                    sym = dec[(static_cast<std::size_t>(1) << len) | acc];
                    if (sym >= 0) {
                        break;
                    }
                }
                if (br.failed()) {
                    break;
                }
                if (sym < 0) {
                    diags.warn("content stream has an invalid method-1 literal code; stopping");
                    break;
                }
                literal = static_cast<std::uint8_t>(sym);
            } else { // method 0: a raw 8-bit byte
                literal = static_cast<std::uint8_t>(br.bits() & 0xFFu);
                br.consume(8);
                if (br.failed()) {
                    break;
                }
            }
            out.push_back(static_cast<std::byte>(literal));
            continue;
        }

        // Match: decode the length symbol.
        const std::size_t sidx = kSymLookup[static_cast<std::size_t>(br.bits() & 0xFFu)];
        br.consume(static_cast<int>(kSymBits[sidx]));
        int value = static_cast<int>(kSymBase[sidx]);
        if (const int sx = static_cast<int>(kSymExtra[sidx]); sx > 0) {
            value += static_cast<int>(br.bits() & ((1u << sx) - 1u));
            br.consume(sx);
        }
        if (br.failed()) {
            break;
        }
        const int symbol = value + 256;
        if (symbol >= 773) { // end-of-stream marker
            ended = true;
            break;
        }
        const int length = symbol - 254;

        // Decode the distance.
        const std::size_t didx = kDistLookup[static_cast<std::size_t>(br.bits() & 0xFFu)];
        br.consume(static_cast<int>(kDistBits[didx]));
        int distance = 0;
        if (length == 2) {
            const int extra = static_cast<int>(br.bits() & 3u);
            br.consume(2);
            distance = ((static_cast<int>(didx) << 2) | extra) + 1;
        } else {
            const int extra = static_cast<int>(br.bits() & extra_mask);
            br.consume(nbits);
            distance = ((static_cast<int>(didx) << nbits) | extra) + 1;
        }
        if (br.failed()) {
            break;
        }

        const auto dist = static_cast<std::size_t>(distance);
        if (dist > out.size()) {
            diags.warn("content stream back-reference precedes start of output; stopping decode "
                       "(stream corrupt or unsupported)");
            return Result<std::vector<std::byte>>::ok(std::move(out));
        }
        if (out.size() + static_cast<std::size_t>(length) > max_output) {
            diags.warn("content decompression hit the size cap mid-match; truncating");
            break;
        }
        // LZ77 copy. Copy byte-by-byte (distances < length are legal overlapping
        // runs) and snapshot each source byte into a local before push_back so a
        // reallocation cannot dangle the source reference.
        const std::size_t src = out.size() - dist;
        for (int i = 0; i < length; ++i) {
            const std::byte b = out[src + static_cast<std::size_t>(i)];
            out.push_back(b);
        }
    }

    if (!ended) {
        if (out.size() >= max_output) {
            diags.warn("content decompression reached the size cap before end-of-stream");
        } else {
            diags.warn("content stream ended without an end-of-stream marker (truncated?)");
        }
    }
    return Result<std::vector<std::byte>>::ok(std::move(out));
}

} // namespace rescore::lzss
