// SPDX-License-Identifier: MIT
//
// entries.cpp - Implementation of the Enigma note "entry" pool reader.

#include "rescore/entries.hpp"

#include "rescore/lzss.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
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

std::size_t find_chain_start(std::span<const std::byte> mus) {
    const auto u8 = [&mus](std::size_t i) { return std::to_integer<unsigned>(mus[i]); };
    // A start is valid if a chunk walk from it parses several known chunk types and
    // consumes most of the file (real .mus files have 4: entries/text/others/details).
    const auto validates = [&](std::size_t start) -> bool {
        std::size_t off = start;
        int chunks = 0;
        while (off + kChunkHeaderSize <= mus.size()) {
            const unsigned type = u8(off) | (u8(off + 1) << 8);
            const std::uint32_t size = u8(off + 2) | (u8(off + 3) << 8) | (u8(off + 4) << 16) |
                                       (u8(off + 5) << 24);
            const bool known = (type >= 15u && type <= 18u) || (type >= 22u && type <= 27u);
            if (!known || size < kChunkHeaderSize || size > mus.size() ||
                off > mus.size() - size) {
                break;
            }
            ++chunks;
            off += size;
        }
        return chunks >= 2 && off >= start + (mus.size() - start) / 2;
    };
    if (mus.size() > kChainStart && validates(kChainStart)) {
        return kChainStart;
    }
    // A larger File-Info header pushes the chain past 0x200: find the first zlib
    // (late-era) chunk body and validate the chain that begins 10 bytes before it.
    for (std::size_t i = kChunkHeaderSize; i + 1 < mus.size(); ++i) {
        if (u8(i) == 0x78u && u8(i + 1) == 0x9Cu && validates(i - kChunkHeaderSize)) {
            return i - kChunkHeaderSize;
        }
    }
    return kChainStart; // fall back to the legacy fixed offset
}

Result<std::vector<EntryRecord>> read_entry_pool(std::span<const std::byte> mus,
                                                 Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    std::vector<EntryRecord> entries;
    std::size_t off = find_chain_start(mus);
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
        if (type != kEntryChunkType && type != kEntryChunkType2011) {
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
            out.id = static_cast<std::uint16_t>(u16r(base));           // entry id at +0
            out.prev_id = static_cast<std::uint16_t>(u16r(base + 6));  // prev-link at +6
            out.next_id = static_cast<std::uint16_t>(u16r(base + 10)); // next-link at +10
            out.duration_edu = static_cast<std::int32_t>(u16r(base + kOffDurationEdu));
            out.in_tuplet = (u8r(base + 22) & 0x08u) != 0u;             // tuplet-membership flag
            out.feature_word = static_cast<std::uint16_t>(u16r(base + 30)); // per-entry flags
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
    std::size_t off = find_chain_start(mus);
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
    std::size_t off = find_chain_start(mus);
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
    std::size_t off = find_chain_start(mus);
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
        if (type != kTextChunkType && type != kTextChunkType2011) {
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

Result<Doc2011> read_doc_2011(std::span<const std::byte> mus, Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    Doc2011 doc;
    std::size_t off = find_chain_start(mus);
    while (off + kChunkHeaderSize <= mus.size()) {
        const std::uint32_t type = u8m(off) | (u8m(off + 1) << 8);
        const std::uint32_t size =
            u8m(off + 2) | (u8m(off + 3) << 8) | (u8m(off + 4) << 16) | (u8m(off + 5) << 24);
        if (size < kChunkHeaderSize || size > mus.size() || off > mus.size() - size) {
            break;
        }
        if (type != kOthersChunkType2011) {
            off += size;
            continue;
        }

        const std::span<const std::byte> stream = mus.subspan(
            off + kChunkHeaderSize, static_cast<std::size_t>(size) - kChunkHeaderSize);
        Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
        if (!inflated) {
            diags.warn("2010+ Others pool could not be decompressed; attributes defaulted");
            break;
        }
        const std::vector<std::byte>& p = inflated.value();
        const auto u16 = [&p](std::size_t at) -> unsigned {
            return std::to_integer<unsigned>(p[at]) | (std::to_integer<unsigned>(p[at + 1]) << 8);
        };

        // One variable-length TLV stream: [tag:2][cmper:2][inci:2][dlen:2][data][6 zero pad].
        std::map<unsigned, std::pair<int, int>> timesig_library; // index -> (beats, divbeat)
        std::map<unsigned, std::string> names;                   // staff cmper -> name
        std::map<unsigned, int> staff_clef_by_cmper;             // staff cmper -> clef index
        std::map<unsigned, unsigned> spec_block;                 // page-text spec -> block id
        std::set<unsigned> title_area_specs;                     // top-centered flagged specs
        std::map<unsigned, std::pair<std::uint16_t, std::uint16_t>>
            artic_glyph_by_cmper; // articDef id -> (glyph code point, font id)
        unsigned ts_index = 0;
        bool got_staff = false;
        std::size_t o = 0;
        int guard = 0;
        while (o + 8 <= p.size() && guard++ < 1000000) {
            const unsigned tag = u16(o);
            const unsigned cmper = u16(o + 2);
            const unsigned dlen = u16(o + 6);
            const std::size_t data = o + 8;
            if (data + dlen + 6 > p.size()) {
                break;
            }
            if (tag == 0x00ADu && dlen >= 6) { // TimeSig library: w1 = beats, w2 = divbeat EDU
                timesig_library[cmper] = {static_cast<int>(u16(data + 2)),
                                          static_cast<int>(u16(data + 4))};
                // The same record family also encodes page-text alignment. A
                // top-centered, flagged page text (halign=4, page-anchor=TOP in
                // the vanchor high byte, flag=2) is a title-area text (the title
                // or the composer). This reads extra fields; it does not change
                // the TimeSig lookup, which is keyed by the staff's time index.
                if (dlen >= 8 && u16(data + 2) == 4 && (u16(data + 4) >> 8) == 0x04u &&
                    u16(data + 6) == 2) {
                    title_area_specs.insert(cmper);
                }
            } else if (tag == 0x00ACu && dlen >= 4) { // page-text spec -> text-block id
                spec_block[cmper] = u16(data + 2);
            } else if (tag == 0x00B9u && dlen >= 6 && !got_staff) { // per-staff key + time index
                doc.key_field = static_cast<std::uint16_t>(u16(data));
                ts_index = u16(data + 4);
                got_staff = true;
            } else if (tag == 0x00BAu && dlen > 2) { // staff name (after a 2-byte prefix)
                std::string name;
                for (std::size_t i = 2; i < dlen; ++i) {
                    const auto ch = std::to_integer<unsigned>(p[data + i]);
                    if (ch == 0) {
                        break;
                    }
                    if (ch >= 0x20 && ch < 0x7F) {
                        name.push_back(static_cast<char>(ch));
                    }
                }
                if (!name.empty()) {
                    names[cmper] = name;
                }
            } else if (tag == 0x00E7u && dlen >= 16) { // per-staff Staff record
                // The default clef index is the byte at data+14 (+15 mirrors it as
                // the transposed clef). cmper 0x7FFF is the document default.
                const int ci = static_cast<int>(std::to_integer<unsigned>(p[data + 14]));
                if (cmper == 0x7FFFu) {
                    doc.default_clef = ci;
                } else {
                    staff_clef_by_cmper[cmper] = ci;
                }
            } else if (tag == 0x0079u && dlen >= 6) { // articulation-definition library
                // cmper = articDef id; main glyph code point at data+2, font id at
                // data+5 (high byte of the font word). Keep the first incidence.
                if (artic_glyph_by_cmper.find(cmper) == artic_glyph_by_cmper.end()) {
                    const auto glyph = static_cast<std::uint16_t>(
                        std::to_integer<unsigned>(p[data + 2]));
                    const auto font = static_cast<std::uint16_t>(
                        std::to_integer<unsigned>(p[data + 5]));
                    if (glyph != 0) {
                        artic_glyph_by_cmper[cmper] = {glyph, font};
                    }
                }
            }
            o += 8 + dlen + 6;
        }

        if (got_staff) {
            const auto it = timesig_library.find(ts_index);
            if (it != timesig_library.end() && it->second.first > 0 && it->second.second > 0) {
                doc.beats = it->second.first;
                doc.divbeat = it->second.second;
            }
            doc.found = true;
        }
        for (const auto& kv : names) {
            doc.staff_names.push_back(kv.second);
            const auto cit = staff_clef_by_cmper.find(kv.first);
            doc.staff_clefs.push_back(cit != staff_clef_by_cmper.end() ? cit->second
                                                                       : doc.default_clef);
        }
        for (const unsigned spec : title_area_specs) {
            const auto it = spec_block.find(spec);
            if (it != spec_block.end()) {
                doc.title_area_blocks.push_back(static_cast<std::uint16_t>(it->second));
            }
        }
        for (const auto& kv : artic_glyph_by_cmper) {
            doc.artic_glyphs[static_cast<std::uint16_t>(kv.first)] = kv.second;
        }
        break; // only the first type-26 chunk
    }

    diags.info(doc.found ? "decoded 2010+ document attributes (key / time / staff names)"
                         : "no 2010+ Others pool found");
    return Result<Doc2011>::ok(std::move(doc));
}

Result<std::vector<Detail2011>> read_details_2011(std::span<const std::byte> mus,
                                                  Diagnostics& diags) {
    const auto u8m = [&mus](std::size_t at) -> unsigned {
        return std::to_integer<unsigned>(mus[at]);
    };

    std::vector<Detail2011> details;
    std::size_t off = find_chain_start(mus);
    while (off + kChunkHeaderSize <= mus.size()) {
        const std::uint32_t type = u8m(off) | (u8m(off + 1) << 8);
        const std::uint32_t size =
            u8m(off + 2) | (u8m(off + 3) << 8) | (u8m(off + 4) << 16) | (u8m(off + 5) << 24);
        if (size < kChunkHeaderSize || size > mus.size() || off > mus.size() - size) {
            break;
        }
        if (type != kDetailsChunkType2011) {
            off += size;
            continue;
        }

        const std::span<const std::byte> stream = mus.subspan(
            off + kChunkHeaderSize, static_cast<std::size_t>(size) - kChunkHeaderSize);
        Result<std::vector<std::byte>> inflated = lzss::inflate_content(stream, diags);
        if (!inflated) {
            diags.warn("2010+ Details pool could not be decompressed; attachments unavailable");
            break;
        }
        const std::vector<std::byte>& p = inflated.value();
        const auto u16 = [&p](std::size_t at) -> unsigned {
            return std::to_integer<unsigned>(p[at]) | (std::to_integer<unsigned>(p[at + 1]) << 8);
        };
        const auto u32 = [&p](std::size_t at) -> std::uint32_t {
            return std::to_integer<std::uint32_t>(p[at]) |
                   (std::to_integer<std::uint32_t>(p[at + 1]) << 8) |
                   (std::to_integer<std::uint32_t>(p[at + 2]) << 16) |
                   (std::to_integer<std::uint32_t>(p[at + 3]) << 24);
        };

        // Record = [class:u16][cmper2:u16][cmper1:u16][inci:u16][len:u32][data:len].
        // Between records, alignment / zero-dword trailers are skipped a dword at a
        // time until the next 2 bytes are a class id we recognize (the resync keeps
        // the walk aligned through the variable-width trailers).
        static constexpr std::uint16_t kKnownClass[] = {
            0x03EF, 0x03F1, 0x03F2, 0x03F3, 0x03F4, 0x03F6, 0x03F7, 0x03F8, 0x040C,
            0x0413, 0x0414, 0x0421, 0x0426, 0x0427, 0x0428, 0x0454, 0x0455};
        const auto is_known = [](unsigned cls) {
            for (const std::uint16_t k : kKnownClass) {
                if (k == cls) {
                    return true;
                }
            }
            return false;
        };

        std::size_t o = 0;
        int guard = 0;
        while (o + 12 <= p.size() && guard++ < 2000000) {
            const unsigned cls = u16(o);
            if (!is_known(cls)) {
                o += 4; // skip a trailer / alignment dword and retry
                continue;
            }
            const std::uint32_t len = u32(o + 8);
            if (len > p.size() || o + 12 + len > p.size()) {
                break; // implausible length: stop rather than read out of bounds
            }
            Detail2011 d;
            d.cls = static_cast<std::uint16_t>(cls);
            d.cmper2 = static_cast<std::uint16_t>(u16(o + 2));
            d.cmper1 = static_cast<std::uint16_t>(u16(o + 4));
            d.inci = static_cast<std::uint16_t>(u16(o + 6));
            d.data.assign(p.begin() + static_cast<std::ptrdiff_t>(o + 12),
                          p.begin() + static_cast<std::ptrdiff_t>(o + 12 + len));
            details.push_back(std::move(d));
            o += 12 + len;
        }
        break; // only the first type-27 chunk
    }

    diags.info("decoded " + std::to_string(details.size()) + " 2010+ detail records");
    return Result<std::vector<Detail2011>>::ok(std::move(details));
}

Result<std::vector<LyricAssign>> read_lyric_assigns_2011(std::span<const std::byte> mus,
                                                         Diagnostics& diags) {
    Result<std::vector<Detail2011>> details = read_details_2011(mus, diags);
    std::vector<LyricAssign> assigns;
    if (details) {
        std::set<std::uint16_t> seen; // each entry has two lyric records; keep one
        for (const Detail2011& d : details.value()) {
            if (d.cls != kClassLyric2011 || d.data.size() < 4) {
                continue;
            }
            if (!seen.insert(d.cmper1).second) {
                continue;
            }
            const auto u16d = [&d](std::size_t at) {
                return std::to_integer<unsigned>(d.data[at]) |
                       (std::to_integer<unsigned>(d.data[at + 1]) << 8);
            };
            LyricAssign a;
            a.entry_id = d.cmper1;
            a.verse = static_cast<int>(u16d(0));
            a.syllable = static_cast<int>(u16d(2));
            assigns.push_back(a);
        }
    }
    diags.info("decoded " + std::to_string(assigns.size()) + " 2010+ lyric assignments");
    return Result<std::vector<LyricAssign>>::ok(std::move(assigns));
}

} // namespace rescore::container
