// SPDX-License-Identifier: MIT
//
// entries.cpp - Implementation of the Enigma note "entry" pool reader.

#include "rescore/entries.hpp"

#include "rescore/lzss.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rescore::container {
namespace {

constexpr std::size_t kChainStart = 0x200;
constexpr std::size_t kChunkHeaderSize = 10;

// A decompressed entry is a fixed 38-byte record. Chords with more than two
// pitches spill into continuation records (a 6-byte header then more notes).
constexpr std::size_t kEntryRecordSize = 38;
constexpr std::size_t kOffDurationEdu = 14; // 2-byte LE EDU (quarter = 0x0400)
constexpr std::size_t kOffNoteCount = 24;   // 0 = rest, N = N sounding pitches
constexpr std::size_t kOffFirstNote = 26;   // first 6-byte note sub-record
constexpr std::size_t kNoteSubSize = 6;     // [TCD:2 LE][2][note-index:1][flags:1]
constexpr unsigned kNotesPerRecord = 2;     // pitches that fit in the main record
constexpr unsigned kNotesPerCont = 5;       // pitches per continuation record

int decode_alter(unsigned tcd) {
    // Alteration is the low nibble of the TCD, as 4-bit sign-magnitude.
    const unsigned nib = tcd & 0x0Fu;
    if ((nib & 0x08u) != 0u) {
        return -static_cast<int>(nib & 0x07u);
    }
    return static_cast<int>(nib);
}

} // namespace

Result<std::vector<EntryRecord>> read_entry_pool(std::span<const std::byte> mus,
                                                 Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    std::vector<EntryRecord> entries;
    std::size_t off = kChainStart;
    while (off + kChunkHeaderSize <= mus.size()) {
        const std::uint32_t type = u8m(off) | (u8m(off + 1) << 8);
        const std::uint32_t size =
            u8m(off + 2) | (u8m(off + 3) << 8) | (u8m(off + 4) << 16) | (u8m(off + 5) << 24);
        if (size < kChunkHeaderSize) {
            break; // end-of-chain / trailer marker
        }
        if (size > mus.size() || off > mus.size() - size) {
            break;
        }
        if (type != kEntryChunkType) {
            off += size;
            continue;
        }

        // The entry chunk body is an LZSS stream (same codec as the Others pool);
        // it inflates to a run of fixed 38-byte records.
        const std::span<const std::byte> stream = mus.subspan(
            off + kChunkHeaderSize, static_cast<std::size_t>(size) - kChunkHeaderSize);
        Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
        if (!inflated) {
            diags.warn("entry chunk could not be decompressed; its notes are skipped");
            off += size;
            continue;
        }
        const std::vector<std::byte>& rec = inflated.value();
        const auto u8r = [&rec](std::size_t at) -> unsigned {
            return std::to_integer<unsigned>(rec[at]);
        };
        const auto u16r = [&u8r](std::size_t at) -> unsigned {
            return u8r(at) | (u8r(at + 1) << 8);
        };
        const auto read_note = [&](std::size_t at) -> EntryNote {
            const unsigned tcd = u16r(at);
            const unsigned flags = u8r(at + 5); // note sub-record flags byte
            EntryNote n;
            n.step_from_c4 = static_cast<int>(static_cast<std::int16_t>(tcd) >> 4);
            n.alter = decode_alter(tcd);
            n.accidental_shown = (flags & 0x01u) != 0u;
            n.tie_start = (flags & 0x40u) != 0u;
            n.tie_stop = (flags & 0x20u) != 0u;
            return n;
        };

        std::size_t base = 0;
        while (base + kEntryRecordSize <= rec.size()) {
            EntryRecord out;
            out.id = static_cast<std::uint16_t>(u16r(base));        // entry id at +0
            out.next_id = static_cast<std::uint16_t>(u16r(base + 10)); // next-link at +10
            out.duration_edu = static_cast<std::int32_t>(u16r(base + kOffDurationEdu));
            out.in_tuplet = (u8r(base + 22) & 0x08u) != 0u; // tuplet-membership flag
            const unsigned note_count = u8r(base + kOffNoteCount);

            unsigned got = 0;
            for (; got < note_count && got < kNotesPerRecord; ++got) {
                out.notes.push_back(read_note(base + kOffFirstNote + got * kNoteSubSize));
            }
            // Notes beyond the first two live in following continuation records.
            std::size_t consumed = kEntryRecordSize;
            std::size_t cont = base + kEntryRecordSize;
            while (got < note_count && cont + kEntryRecordSize <= rec.size()) {
                for (unsigned slot = 0; got < note_count && slot < kNotesPerCont; ++slot, ++got) {
                    out.notes.push_back(read_note(cont + kNoteSubSize + slot * kNoteSubSize));
                }
                cont += kEntryRecordSize;
                consumed += kEntryRecordSize;
            }

            entries.push_back(std::move(out));
            base += consumed;
        }
        off += size;
    }

    diags.info("decoded " + std::to_string(entries.size()) + " entries from the entry pool");
    return Result<std::vector<EntryRecord>>::ok(std::move(entries));
}

Result<std::vector<FrameHolder>> read_frame_holders(std::span<const std::byte> mus,
                                                    Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    std::vector<FrameHolder> holders;
    std::size_t off = kChainStart;
    while (off + kChunkHeaderSize <= mus.size()) {
        const std::uint32_t type = u8m(off) | (u8m(off + 1) << 8);
        const std::uint32_t size =
            u8m(off + 2) | (u8m(off + 3) << 8) | (u8m(off + 4) << 16) | (u8m(off + 5) << 24);
        if (size < kChunkHeaderSize) {
            break;
        }
        if (size > mus.size() || off > mus.size() - size) {
            break;
        }
        if (type != kDetailsChunkType) {
            off += size;
            continue;
        }

        // The Details pool is LZSS-compressed and inflates to 16-byte records.
        const std::span<const std::byte> stream = mus.subspan(
            off + kChunkHeaderSize, static_cast<std::size_t>(size) - kChunkHeaderSize);
        Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
        if (!inflated) {
            diags.warn("details pool could not be decompressed; staff layout unavailable");
            break;
        }
        const std::vector<std::byte>& rec = inflated.value();
        const auto u8r = [&rec](std::size_t at) -> unsigned {
            return std::to_integer<unsigned>(rec[at]);
        };
        const auto u16r = [&u8r](std::size_t at) -> unsigned {
            return u8r(at) | (u8r(at + 1) << 8);
        };
        // A 16-byte Details record is [cmper1:2][cmper2:2][tag:2][5 words]. Frame
        // holders carry the tag "FG" (0x46 0x47), with cmper1 = staff, cmper2 =
        // measure, and the first entry id in the third data word (offset +10).
        constexpr std::size_t kDetailRec = 16;
        for (std::size_t base = 0; base + kDetailRec <= rec.size(); base += kDetailRec) {
            if (u8r(base + 4) == 0x46u && u8r(base + 5) == 0x47u) {
                FrameHolder h;
                h.staff = static_cast<std::uint16_t>(u16r(base));
                h.measure = static_cast<std::uint16_t>(u16r(base + 2));
                h.clef = static_cast<std::uint16_t>(u16r(base + 6));            // data word 0
                h.voice_first[0] = static_cast<std::uint16_t>(u16r(base + 10)); // word 2 = layer 1
                h.voice_first[1] = static_cast<std::uint16_t>(u16r(base + 12)); // word 3 = layer 2
                h.voice_first[2] = static_cast<std::uint16_t>(u16r(base + 14)); // word 4 = layer 3
                holders.push_back(h);
            }
        }
        break; // only the first Details chunk holds the frame holders
    }

    diags.info("read " + std::to_string(holders.size()) + " frame-holder records");
    return Result<std::vector<FrameHolder>>::ok(std::move(holders));
}

Result<std::vector<DetailRecord>> read_details_pool(std::span<const std::byte> mus,
                                                    Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    std::vector<DetailRecord> records;
    std::size_t off = kChainStart;
    while (off + kChunkHeaderSize <= mus.size()) {
        const std::uint32_t type = u8m(off) | (u8m(off + 1) << 8);
        const std::uint32_t size =
            u8m(off + 2) | (u8m(off + 3) << 8) | (u8m(off + 4) << 16) | (u8m(off + 5) << 24);
        if (size < kChunkHeaderSize) {
            break;
        }
        if (size > mus.size() || off > mus.size() - size) {
            break;
        }
        if (type != kDetailsChunkType) {
            off += size;
            continue;
        }

        const std::span<const std::byte> stream = mus.subspan(
            off + kChunkHeaderSize, static_cast<std::size_t>(size) - kChunkHeaderSize);
        Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
        if (!inflated) {
            diags.warn("details pool could not be decompressed; attachments unavailable");
            break;
        }
        const std::vector<std::byte>& rec = inflated.value();
        const auto u8r = [&rec](std::size_t at) -> unsigned {
            return std::to_integer<unsigned>(rec[at]);
        };
        const auto u16r = [&u8r](std::size_t at) -> unsigned {
            return u8r(at) | (u8r(at + 1) << 8);
        };
        constexpr std::size_t kDetailRec = 16;
        for (std::size_t base = 0; base + kDetailRec <= rec.size(); base += kDetailRec) {
            DetailRecord d;
            d.cmper = static_cast<std::uint16_t>(u16r(base));
            d.inci = static_cast<std::uint16_t>(u16r(base + 2));
            d.tag = static_cast<std::uint16_t>(u16r(base + 4));
            for (std::size_t w = 0; w < d.data.size(); ++w) {
                d.data[w] = static_cast<std::uint16_t>(u16r(base + 6 + w * 2));
            }
            records.push_back(d);
        }
        break; // only the first Details chunk holds the attachments
    }

    diags.info("read " + std::to_string(records.size()) + " details records");
    return Result<std::vector<DetailRecord>>::ok(std::move(records));
}

Result<std::string> read_text_pool(std::span<const std::byte> mus, Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    std::string text;
    std::size_t off = kChainStart;
    while (off + kChunkHeaderSize <= mus.size()) {
        const std::uint32_t type = u8m(off) | (u8m(off + 1) << 8);
        const std::uint32_t size =
            u8m(off + 2) | (u8m(off + 3) << 8) | (u8m(off + 4) << 16) | (u8m(off + 5) << 24);
        if (size < kChunkHeaderSize) {
            break;
        }
        if (size > mus.size() || off > mus.size() - size) {
            break;
        }
        if (type != kTextChunkType) {
            off += size;
            continue;
        }

        const std::span<const std::byte> stream = mus.subspan(
            off + kChunkHeaderSize, static_cast<std::size_t>(size) - kChunkHeaderSize);
        Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
        if (inflated) {
            const std::vector<std::byte>& body = inflated.value();
            text.reserve(text.size() + body.size());
            for (const std::byte b : body) {
                text.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
            }
        } else {
            diags.warn("text pool could not be decompressed; lyrics unavailable");
        }
        off += size;
    }
    return Result<std::string>::ok(std::move(text));
}

} // namespace rescore::container
