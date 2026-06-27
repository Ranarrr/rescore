// SPDX-License-Identifier: MIT
//
// entries.hpp - The Enigma note "entry" pool and frame holders (Layer 1.5).
//
// Note entries live in an LZSS-compressed chunk (type 17) and inflate to fixed
// 38-byte records: an id, a next-link, a duration, a note-count, and per-note
// pitch/alteration. The next-links chain a staff's notes together. The frame
// holders (in the type-16 Details pool, also compressed) say which (staff,
// measure) cell a chain belongs to, which is how a note knows its staff. Every
// read is bounds-checked - untrusted input never faults.

#ifndef RESCORE_ENTRIES_HPP
#define RESCORE_ENTRIES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "rescore/result.hpp"

namespace rescore::container {

/// One sounding pitch within an entry. `step_from_c4` is a diatonic staff-step
/// counter with 0 = middle C (C4), +1 per step; `alter` is the semitone
/// alteration (-2..+2), decoded from the on-disk sign-magnitude nibble.
struct EntryNote {
    int step_from_c4{0};
    int alter{0};
    bool accidental_shown{false};
    bool tie_start{false}; // this note ties to the next same-pitch note
    bool tie_stop{false};  // this note is tied from the previous note
};

/// One rhythmic event: a duration plus zero or more pitches. An empty `notes`
/// means a rest; one note is a single pitch; more than one is a chord. `id` is
/// this entry's number; `next_id` is the next entry in the same staff's chain
/// (0 ends the chain).
struct EntryRecord {
    std::uint16_t id{0};
    std::uint16_t next_id{0};
    std::int32_t duration_edu{1024}; // displayed note value (e.g. eighth = 512)
    bool in_tuplet{false};           // this entry is part of a tuplet group
    std::vector<EntryNote> notes;
};

/// A frame holder: a (staff, measure) cell and the first entry id of the note
/// chain it holds. A staff's whole note chain is reachable from its lowest-measure
/// holder by following EntryRecord::next_id.
struct FrameHolder {
    std::uint16_t staff{0};
    std::uint16_t measure{0};
    std::uint16_t clef{0}; // clef index for this cell (treble = 0, bass = 3)
    /// First entry id of each layer/voice in this cell (0 = empty). Each layer is
    /// an independent next-link chain that becomes one MusicXML voice.
    std::array<std::uint16_t, 3> voice_first{};
};

/// A generic Details-pool record: 16 bytes = [cmper:2][inci:2][tag:2][data:5
/// words]. Per-entry / per-measure attachments (frame holders, smart-shape
/// endpoints, articulations, lyrics) share this shape; `tag` selects the meaning.
struct DetailRecord {
    std::uint16_t cmper{0};
    std::uint16_t inci{0};
    std::uint16_t tag{0}; // little-endian word at record offset +4
    std::array<std::uint16_t, 5> data{};
};

/// Details-pool tags. On disk the two ASCII bytes are stored low byte first, so
/// e.g. 'xE' (bytes 78 45) reads as the little-endian word 0x4578.
inline constexpr std::uint16_t kDetailTagSmartShapeEntry = 0x4578; // 'xE'
inline constexpr std::uint16_t kDetailTagArtic = 0x494D;           // 'MI'
inline constexpr std::uint16_t kDetailTagLyricEntry = 0x7665;      // 'ev'

/// Chunk types in the .mus chunk chain.
inline constexpr std::uint16_t kEntryChunkType = 17;   // note entries
inline constexpr std::uint16_t kDetailsChunkType = 16; // Details pool (frame holders)
inline constexpr std::uint16_t kTextChunkType = 18;    // Enigma Text pool (verse text)

/// Walk the chunk chain and decode the note entries from the entry chunk(s).
/// Returns the entries (possibly empty, with a diagnostic). Never throws.
[[nodiscard]] Result<std::vector<EntryRecord>> read_entry_pool(std::span<const std::byte> mus,
                                                               Diagnostics& diags);

/// Decompress the type-16 Details pool and read the frame-holder records mapping
/// (staff, measure) cells to their first entry id. Returns them (possibly empty).
/// Never throws, never reads out of bounds.
[[nodiscard]] Result<std::vector<FrameHolder>> read_frame_holders(std::span<const std::byte> mus,
                                                                  Diagnostics& diags);

/// Decompress the type-16 Details pool and return every 16-byte record. Callers
/// filter by `tag` (smart-shape endpoints 'xE', articulations 'MI', lyric
/// assignments 'ev'). Returns them (possibly empty). Never throws.
[[nodiscard]] Result<std::vector<DetailRecord>> read_details_pool(std::span<const std::byte> mus,
                                                                  Diagnostics& diags);

/// Decompress the type-18 Text pool (LZSS, may use method 1) and return its raw
/// ASCII bytes: a run of '^command(args)' text blocks holding verse/lyric text.
/// Returns the concatenated text of all type-18 chunks (possibly empty).
[[nodiscard]] Result<std::string> read_text_pool(std::span<const std::byte> mus,
                                                 Diagnostics& diags);

} // namespace rescore::container

#endif // RESCORE_ENTRIES_HPP
