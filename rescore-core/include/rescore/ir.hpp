// SPDX-License-Identifier: MIT
//
// ir.hpp - Layer 2, the clean intermediate representation (namespace
// rescore::ir). This is the stable CONTRACT between the binary parser (Layer 1)
// and the MusicXML emitter (Layer 3). It is notation-program-agnostic: Enigma
// idioms must resolve into musical meaning before reaching here, and no
// MusicXML concern may leak in.
//
// These types are the canonical project IR and must match the shared
// conventions exactly. They are plain value types, unit-testable with neither
// a binary file nor an XML emitter present.

#ifndef RESCORE_IR_HPP
#define RESCORE_IR_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rescore::ir {

/// Diatonic step (note letter name).
enum class Step { C, D, E, F, G, A, B };

/// A concert pitch. `alter` is in semitones; the MVP subset uses only -1, 0, +1
/// (flat / natural / sharp).
struct Pitch {
    Step step{Step::C};
    int octave{4};
    int alter{0};
};

/// Clef glyph family.
enum class ClefSign { G, F, C, Percussion };

/// A clef: a sign plus the staff line it sits on (1 = bottom line). Treble is
/// {G, 2}; bass is {F, 4}.
struct Clef {
    ClefSign sign{ClefSign::G};
    int line{2};
    int octave_change{0}; // -1 = treble_8 (vocal tenor), +1 = treble^8, etc.
};

/// A key signature expressed in circle-of-fifths steps (sharps positive, flats
/// negative), with an optional mode.
struct KeySignature {
    int fifths{0};
    enum class Mode { Major, Minor, None } mode{Mode::Major};
};

/// A time signature: {4, 4} means 4/4.
struct TimeSignature {
    int beats{4};
    int beat_type{4};
};

/// Finale's elapsed-duration unit. A quarter note is 1024 EDU.
using Edu = std::int32_t;

/// EDU constants for the MVP rhythmic subset.
inline constexpr Edu kEduPerQuarter = 1024;
inline constexpr Edu kEduWhole = 4096;
inline constexpr Edu kEduHalf = 2048;
inline constexpr Edu kEduQuarter = 1024;
inline constexpr Edu kEduEighth = 512;
inline constexpr Edu kEduSixteenth = 256;

/// Symbolic note-value names supported by the MVP.
enum class NoteTypeName { Whole, Half, Quarter, Eighth, Sixteenth };

/// A resolved note value: base name plus augmentation dots.
struct NoteType {
    NoteTypeName name{NoteTypeName::Quarter};
    int dots{0};
};

/// A single sounding pitch within an entry, with tie endpoints.
struct Note {
    Pitch pitch;
    bool tie_start{false};
    bool tie_stop{false};
};

/// A note articulation mark. `StrongAccent` is MusicXML's marcato (the ^ wedge).
enum class Articulation {
    Staccato,
    Accent,
    Tenuto,
    StrongAccent,
    Staccatissimo,
    DetachedLegato,
    Fermata,
};

/// One lyric syllable attached to an entry. `verse` is the 1-based verse line
/// (MusicXML lyric number); `syllabic` records the hyphenation role.
struct Lyric {
    int verse{1};
    enum class Syllabic { Single, Begin, Middle, End } syllabic{Syllabic::Single};
    std::string text;
};

/// A tuplet ratio: `actual` notes performed in the time of `normal` (a triplet is
/// {3, 2}). {0, 0} means the entry is not part of a tuplet.
struct TimeModification {
    int actual_notes{0};
    int normal_notes{0};
};

/// One rhythmic event in a voice: a rest, a single note, or a chord (when
/// `notes.size() > 1`). `duration` is the authoritative (sounding) EDU value;
/// `type` is the symbolic note value drawn (the displayed value for tuplets).
struct Entry {
    Edu duration{kEduQuarter};
    bool is_rest{false};
    NoteType type{};
    std::vector<Note> notes;
    TimeModification time_mod{}; // tuplet ratio; {0, 0} when not a tuplet
    bool tuplet_start{false};    // first entry of a tuplet bracket
    bool tuplet_stop{false};     // last entry of a tuplet bracket
    int slur_start{0};           // slur number starting on this entry (0 = none)
    int slur_stop{0};            // slur number stopping on this entry (0 = none)
    std::vector<Articulation> articulations; // marks attached to this entry
    std::vector<Lyric> lyrics;               // syllables under this entry
    std::vector<std::string> dynamics; // dynamic marks at this entry, hoisted to the
                                       // measure as <direction>s at the entry's beat
};

/// A single rhythmic stream (one layer/voice) within a measure.
struct Voice {
    std::vector<Entry> entries;
};

/// A measure-level direction: a dynamic mark or a hairpin (wedge) endpoint,
/// anchored at an EDU offset from the measure start.
struct Direction {
    enum class Kind { Dynamic, WedgeStart, WedgeStop } kind{Kind::Dynamic};
    std::string dynamic;       // dynamic text (e.g. "f", "p") when kind == Dynamic
    bool crescendo{true};      // when kind == WedgeStart: crescendo vs diminuendo
    Edu position{0};           // EDU offset within the measure (beat anchor)
    std::uint16_t staff{0};    // owning staff number (0 = default / first part)
};

/// A measure. Attribute changes (key/time/clef) are present only when they
/// change at this measure (the first measure typically carries all three).
struct Measure {
    std::optional<KeySignature> key;
    std::optional<TimeSignature> time;
    std::optional<Clef> clef;
    std::vector<Voice> voices;
    std::vector<Direction> directions; // measure-level dynamics / hairpins
};

/// A staff: an initial clef plus its sequence of measures.
struct Staff {
    Clef initial_clef;
    std::vector<Measure> measures;
};

/// A part (instrument): a stable id, a display name, and one or more staves.
struct Part {
    std::string id;
    std::string name;
    std::vector<Staff> staves;
};

/// The whole document.
struct Score {
    std::string work_title;
    std::string composer;
    std::vector<Part> parts;
};

/// Resolve an EDU duration into a symbolic NoteType (name + dots) for the MVP
/// subset (whole, half, quarter, eighth, sixteenth; single dot). An EDU that
/// does not match a supported value resolves to the nearest supported value.
[[nodiscard]] NoteType resolve_note_type(Edu duration);

} // namespace rescore::ir

#endif // RESCORE_IR_HPP
