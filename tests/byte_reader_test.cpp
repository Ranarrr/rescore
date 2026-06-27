// tests/byte_reader_test.cpp
//
// Unit tests for the Layer 1 bounds-checked ByteReader (namespace rescore).
//
// The archival .mus inputs are untrusted, so the reader's defining contract is
// "never read out of bounds": every read that would run past the end of the
// buffer must return an empty std::optional rather than dereference past the
// span. These tests pin that contract plus little-endian / big-endian decoding
// (early Mac Finale files are big-endian; Windows files are little-endian) and
// exact-boundary reads (a read that consumes the final byte must succeed; the
// very next read must fail).

#include <rescore/byte_reader.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

using rescore::ByteReader;

namespace {

// Build a byte buffer from integer literals (each must fit in a byte). The
// reader operates on std::span<const std::byte>, so test fixtures are std::byte.
std::vector<std::byte> make_bytes(std::initializer_list<int> vals) {
    std::vector<std::byte> out;
    out.reserve(vals.size());
    for (int v : vals) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

// 0x01 0x02 0x03 0x04 - handy known bytes for endianness assertions.
const std::vector<std::byte> kBytes = make_bytes({0x01, 0x02, 0x03, 0x04});

} // namespace

TEST_CASE("ByteReader: empty buffer yields no bytes", "[byte_reader]") {
    ByteReader reader(std::span<const std::byte>{});
    REQUIRE(reader.remaining() == 0);
    REQUIRE_FALSE(reader.read_u8().has_value());
    // A failed read must not advance the cursor or otherwise corrupt state.
    REQUIRE(reader.remaining() == 0);
    REQUIRE_FALSE(reader.read_u16le().has_value());
    REQUIRE_FALSE(reader.read_u32be().has_value());
}

TEST_CASE("ByteReader: sequential single-byte reads", "[byte_reader]") {
    ByteReader reader(kBytes);
    REQUIRE(reader.remaining() == 4);

    auto b0 = reader.read_u8();
    REQUIRE(b0.has_value());
    REQUIRE(*b0 == 0x01);
    REQUIRE(reader.remaining() == 3);

    auto b1 = reader.read_u8();
    REQUIRE(b1.has_value());
    REQUIRE(*b1 == 0x02);
    REQUIRE(reader.remaining() == 2);
}

TEST_CASE("ByteReader: little-endian vs big-endian 16-bit", "[byte_reader]") {
    SECTION("u16 little-endian") {
        ByteReader reader(kBytes);
        auto v = reader.read_u16le();
        REQUIRE(v.has_value());
        // 0x01, 0x02 -> low byte first -> 0x0201.
        REQUIRE(*v == 0x0201u);
        REQUIRE(reader.remaining() == 2);
    }
    SECTION("u16 big-endian") {
        ByteReader reader(kBytes);
        auto v = reader.read_u16be();
        REQUIRE(v.has_value());
        // 0x01, 0x02 -> high byte first -> 0x0102.
        REQUIRE(*v == 0x0102u);
        REQUIRE(reader.remaining() == 2);
    }
}

TEST_CASE("ByteReader: little-endian vs big-endian 32-bit", "[byte_reader]") {
    SECTION("u32 little-endian") {
        ByteReader reader(kBytes);
        auto v = reader.read_u32le();
        REQUIRE(v.has_value());
        // 0x01 0x02 0x03 0x04 -> low byte first -> 0x04030201.
        REQUIRE(*v == 0x04030201u);
        REQUIRE(reader.remaining() == 0);
    }
    SECTION("u32 big-endian") {
        ByteReader reader(kBytes);
        auto v = reader.read_u32be();
        REQUIRE(v.has_value());
        // 0x01 0x02 0x03 0x04 -> high byte first -> 0x01020304.
        REQUIRE(*v == 0x01020304u);
        REQUIRE(reader.remaining() == 0);
    }
}

TEST_CASE("ByteReader: exact-boundary read of the final byte succeeds",
          "[byte_reader]") {
    // Consume the first three bytes, leaving exactly one byte. A u8 read must
    // succeed and drain the buffer to empty.
    ByteReader reader(kBytes);
    REQUIRE(reader.read_u8().has_value()); // 0x01
    REQUIRE(reader.read_u8().has_value()); // 0x02
    REQUIRE(reader.read_u8().has_value()); // 0x03
    REQUIRE(reader.remaining() == 1);

    auto last = reader.read_u8();
    REQUIRE(last.has_value());
    REQUIRE(*last == 0x04);
    REQUIRE(reader.remaining() == 0);

    // One past the end: graceful empty optional, no crash, cursor unchanged.
    REQUIRE_FALSE(reader.read_u8().has_value());
    REQUIRE(reader.remaining() == 0);
}

TEST_CASE("ByteReader: exact-boundary multi-byte read succeeds, overrun fails",
          "[byte_reader]") {
    // A u32 read consumes the whole 4-byte buffer to the exact boundary.
    ByteReader reader(kBytes);
    auto whole = reader.read_u32le();
    REQUIRE(whole.has_value());
    REQUIRE(reader.remaining() == 0);
    // The next u32 would need four more bytes that do not exist.
    REQUIRE_FALSE(reader.read_u32le().has_value());
}

TEST_CASE("ByteReader: partial overrun is rejected atomically", "[byte_reader]") {
    // Three bytes available, but a u32 needs four. The read must fail WITHOUT
    // partially consuming the three present bytes (no torn / partial reads).
    const std::vector<std::byte> three = make_bytes({0xDE, 0xAD, 0xBE});
    ByteReader reader(three);
    REQUIRE(reader.remaining() == 3);
    REQUIRE_FALSE(reader.read_u32le().has_value());
    REQUIRE(reader.remaining() == 3);

    // The bytes must still be fully readable after the rejected wide read.
    auto a = reader.read_u8();
    auto b = reader.read_u8();
    auto c = reader.read_u8();
    REQUIRE(a.has_value());
    REQUIRE(*a == 0xDE);
    REQUIRE(b.has_value());
    REQUIRE(*b == 0xAD);
    REQUIRE(c.has_value());
    REQUIRE(*c == 0xBE);
    REQUIRE(reader.remaining() == 0);
}
