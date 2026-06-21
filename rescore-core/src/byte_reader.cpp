// SPDX-License-Identifier: MIT
//
// byte_reader.cpp - Implementation of the bounds-checked ByteReader.
//
// Integer assembly is done byte-by-byte from the borrowed span so that the
// code is endianness-agnostic with respect to the HOST and never relies on
// unaligned reinterpret_cast (which would be UB on some targets).

#include "rescore/byte_reader.hpp"

namespace rescore {

std::optional<std::span<const std::byte>> ByteReader::take(std::size_t n) noexcept {
    if (n > remaining()) {
        return std::nullopt;
    }
    std::span<const std::byte> view = data_.subspan(pos_, n);
    pos_ += n;
    return view;
}

std::optional<std::uint8_t> ByteReader::read_u8() noexcept {
    auto view = take(1);
    if (!view) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>((*view)[0]);
}

std::optional<std::uint8_t> ByteReader::peek_u8() const noexcept {
    if (pos_ >= data_.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(data_[pos_]);
}

std::optional<std::uint16_t> ByteReader::read_u16le() noexcept {
    auto view = take(2);
    if (!view) {
        return std::nullopt;
    }
    const auto b0 = static_cast<std::uint16_t>((*view)[0]);
    const auto b1 = static_cast<std::uint16_t>((*view)[1]);
    return static_cast<std::uint16_t>(b0 | (b1 << 8));
}

std::optional<std::uint16_t> ByteReader::read_u16be() noexcept {
    auto view = take(2);
    if (!view) {
        return std::nullopt;
    }
    const auto b0 = static_cast<std::uint16_t>((*view)[0]);
    const auto b1 = static_cast<std::uint16_t>((*view)[1]);
    return static_cast<std::uint16_t>((b0 << 8) | b1);
}

std::optional<std::uint32_t> ByteReader::read_u32le() noexcept {
    auto view = take(4);
    if (!view) {
        return std::nullopt;
    }
    const auto b0 = static_cast<std::uint32_t>((*view)[0]);
    const auto b1 = static_cast<std::uint32_t>((*view)[1]);
    const auto b2 = static_cast<std::uint32_t>((*view)[2]);
    const auto b3 = static_cast<std::uint32_t>((*view)[3]);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

std::optional<std::uint32_t> ByteReader::read_u32be() noexcept {
    auto view = take(4);
    if (!view) {
        return std::nullopt;
    }
    const auto b0 = static_cast<std::uint32_t>((*view)[0]);
    const auto b1 = static_cast<std::uint32_t>((*view)[1]);
    const auto b2 = static_cast<std::uint32_t>((*view)[2]);
    const auto b3 = static_cast<std::uint32_t>((*view)[3]);
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

std::optional<std::uint16_t> ByteReader::read_u16(Endian endian) noexcept {
    return endian == Endian::Little ? read_u16le() : read_u16be();
}

std::optional<std::uint32_t> ByteReader::read_u32(Endian endian) noexcept {
    return endian == Endian::Little ? read_u32le() : read_u32be();
}

std::optional<std::vector<std::byte>> ByteReader::read_bytes(std::size_t n) {
    auto view = take(n);
    if (!view) {
        return std::nullopt;
    }
    return std::vector<std::byte>(view->begin(), view->end());
}

std::optional<std::string> ByteReader::read_ascii(std::size_t n) {
    auto view = take(n);
    if (!view) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(n);
    for (std::byte b : *view) {
        const auto c = static_cast<unsigned char>(b);
        if (c == 0) {
            break; // NUL terminates the logical string; cursor already advanced.
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

} // namespace rescore
