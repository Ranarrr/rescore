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

/// One rhythmic event in a voice: a rest, a single note, or a chord (when
/// `notes.size() > 1`). `duration` is the authoritative EDU value; `type` is the
/// symbolic rendering derived from it.
struct Entry {
    Edu duration{kEduQuarter};
    bool is_rest{false};
    NoteType type{};
    std::vector<Note> notes;
};

/// A single rhythmic stream (one layer/voice) within a measure.
struct Voice {
    std::vector<Entry> entries;
};

/// A measure. Attribute changes (key/time/clef) are present only when they
/// change at this measure (the first measure typically carries all three).
struct Measure {
    std::optional<KeySignature> key;
    std::optional<TimeSignature> time;
    std::optional<Clef> clef;
    std::vector<Voice> voices;
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
    std::vector<Part> parts;
};

/// Resolve an EDU duration into a symbolic NoteType (name + dots) for the MVP
/// subset (whole, half, quarter, eighth, sixteenth; single dot). An EDU that
/// does not match a supported value resolves to the nearest supported value.
[[nodiscard]] NoteType resolve_note_type(Edu duration);

} // namespace rescore::ir

#endif // RESCORE_IR_HPP
