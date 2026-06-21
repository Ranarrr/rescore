// SPDX-License-Identifier: MIT
//
// byte_reader.hpp - The security keystone.
//
// ByteReader is the ONLY sanctioned way to read bytes out of an untrusted
// .mus buffer. Every accessor is bounds-checked and returns std::optional;
// a read that would run past the end of the span yields std::nullopt instead
// of invoking undefined behavior. No method ever performs unchecked pointer
// arithmetic, reads out of bounds, or throws.
//
// Endianness matters: earliest Mac legacy .mus files are big-endian while
// Windows-era files are little-endian, so both byte orders are first-class.

#ifndef RESCORE_BYTE_READER_HPP
#define RESCORE_BYTE_READER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rescore {

/// Byte order selector for the width-parameterized readers.
enum class Endian { Little, Big };

/// A forward, bounds-checked cursor over a borrowed byte buffer.
///
/// The reader does not own its data; the caller must keep the underlying
/// storage alive for the reader's lifetime. All read_* methods advance the
/// cursor only on success; on failure the cursor is left unchanged.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> data) noexcept : data_(data) {}

    /// Total size of the underlying buffer in bytes.
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    /// Current cursor position (0-based byte offset).
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }

    /// Bytes remaining between the cursor and the end of the buffer.
    [[nodiscard]] std::size_t remaining() const noexcept {
        return data_.size() - pos_;
    }

    /// True once the cursor has reached the end of the buffer.
    [[nodiscard]] bool eof() const noexcept { return pos_ >= data_.size(); }

    /// Move the cursor to an absolute offset. Returns false (and leaves the
    /// cursor unchanged) if `offset` is past the end of the buffer.
    [[nodiscard]] bool seek(std::size_t offset) noexcept {
        if (offset > data_.size()) {
            return false;
        }
        pos_ = offset;
        return true;
    }

    /// Advance the cursor by `n` bytes. Returns false if that would overrun.
    [[nodiscard]] bool skip(std::size_t n) noexcept {
        if (n > remaining()) {
            return false;
        }
        pos_ += n;
        return true;
    }

    /// Read a single unsigned byte.
    [[nodiscard]] std::optional<std::uint8_t> read_u8() noexcept;

    /// Read a 16-bit unsigned integer, little- or big-endian.
    [[nodiscard]] std::optional<std::uint16_t> read_u16le() noexcept;
    [[nodiscard]] std::optional<std::uint16_t> read_u16be() noexcept;

    /// Read a 32-bit unsigned integer, little- or big-endian.
    [[nodiscard]] std::optional<std::uint32_t> read_u32le() noexcept;
    [[nodiscard]] std::optional<std::uint32_t> read_u32be() noexcept;

    /// Endian-parameterized convenience wrappers.
    [[nodiscard]] std::optional<std::uint16_t> read_u16(Endian endian) noexcept;
    [[nodiscard]] std::optional<std::uint32_t> read_u32(Endian endian) noexcept;

    /// Read exactly `n` raw bytes. Returns nullopt if fewer than `n` remain.
    [[nodiscard]] std::optional<std::vector<std::byte>> read_bytes(std::size_t n);

    /// Read exactly `n` bytes and interpret them as an ASCII/Latin-1 string.
    /// A NUL within the range terminates the returned string but the cursor
    /// still advances by the full `n`. Returns nullopt if fewer than `n` remain.
    [[nodiscard]] std::optional<std::string> read_ascii(std::size_t n);

    /// Non-advancing peek of a single byte at the current cursor.
    [[nodiscard]] std::optional<std::uint8_t> peek_u8() const noexcept;

    /// A non-owning view of the underlying buffer (for previews/hex dumps).
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return data_; }

private:
    /// Returns a view of `n` bytes at the cursor and advances, or nullopt.
    [[nodiscard]] std::optional<std::span<const std::byte>> take(std::size_t n) noexcept;

    std::span<const std::byte> data_;
    std::size_t pos_{0};
};

} // namespace rescore

#endif // RESCORE_BYTE_READER_HPP
