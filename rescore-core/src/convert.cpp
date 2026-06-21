// SPDX-License-Identifier: MIT
//
// convert.cpp - End-to-end pipeline facade, the IR attribute mappings, and the
// canonical trivial-score fixture.

#include "rescore/convert.hpp"

#include "rescore/musicxml.hpp"
#include "rescore/others.hpp"
#include "rescore/version.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rescore {

// ----------------------------------------------------------------------------
// Enigma measure-spec -> IR attribute mappings (pure, total).
// ----------------------------------------------------------------------------

ir::TimeSignature derive_time_signature(int beats, int divbeat) {
    constexpr int kWhole = ir::kEduWhole; // 4096 EDU per whole note
    if (beats <= 0 || divbeat <= 0) {
        return ir::TimeSignature{4, 4};
    }
    // Simple meter: divbeat is an undotted (power-of-two) note value, so the
    // displayed denominator is whole-note / divbeat and the numerator is beats.
    if ((divbeat & (divbeat - 1)) == 0 && kWhole % divbeat == 0) {
        return ir::TimeSignature{beats, kWhole / divbeat};
    }
    // Compound meter: divbeat is a dotted note value (= 3 * 2^k). Each beat
    // subdivides into three; the displayed denominator is that subdivision note.
    if (divbeat % 3 == 0) {
        const int subdivision = divbeat / 3;
        if (subdivision > 0 && (subdivision & (subdivision - 1)) == 0 &&
            kWhole % subdivision == 0) {
            return ir::TimeSignature{beats * 3, kWhole / subdivision};
        }
    }
    return ir::TimeSignature{beats, 4};
}

ir::Clef clef_from_index(int clef_index) {
    switch (clef_index) {
    case 0:
        return ir::Clef{ir::ClefSign::G, 2}; // treble
    case 3:
        return ir::Clef{ir::ClefSign::F, 4}; // bass
    case 1:
        return ir::Clef{ir::ClefSign::C, 3}; // alto (conventional)
    case 2:
        return ir::Clef{ir::ClefSign::C, 4}; // tenor (conventional)
    default:
        return ir::Clef{ir::ClefSign::G, 2}; // fallback: treble
    }
}

ir::KeySignature key_from_field(std::uint16_t key_field) {
    ir::KeySignature key;
    key.fifths = static_cast<std::int8_t>(key_field & 0xFFu); // signed accidental count
    const int bank = static_cast<int>((key_field >> 8) & 0x3Fu);
    key.mode = (bank == 1) ? ir::KeySignature::Mode::Minor : ir::KeySignature::Mode::Major;
    return key;
}

namespace {

/// Layer 2: build an ir::Score from the decoded Enigma "Other" records. Today
/// this resolves the per-measure key/time and the staff clef; note (entry pool)
/// decoding is a separate piece of work, so the measures carry attributes but no
/// notes. Returns an Error if there are no measure specs to build from.
[[nodiscard]] Result<ir::Score>
build_score_from_others(const std::vector<container::OtherRecord>& others, Diagnostics& diags) {
    using namespace ir;

    // Clef: staff 1's clef index is data[0] of the SECOND Staff-Spec incidence
    // (the first IS incidence holds other fields).
    Clef clef{ClefSign::G, 2};
    {
        int incidence = 0;
        for (const auto& record : others) {
            if (record.tag == container::kTagStaffSpec && record.cmper == 1) {
                if (++incidence == 2) {
                    clef = clef_from_index(static_cast<int>(record.data[0]));
                    break;
                }
            }
        }
        if (incidence < 2) {
            diags.warn("staff-spec clef record not found; defaulting to treble clef");
        }
    }

    // Measure specs: the FIRST Measure-Spec record for each measure number is
    // incidence 1, carrying [measspace, key, beats, divbeat, ...]. Keyed by cmper
    // so the map iterates measures in ascending order.
    std::map<std::uint16_t, const container::OtherRecord*> measure_specs;
    for (const auto& record : others) {
        if (record.tag == container::kTagMeasureSpec) {
            measure_specs.emplace(record.cmper, &record); // keeps the first per cmper
        }
    }
    if (measure_specs.empty()) {
        return Result<Score>::fail(ErrorCode::NotImplemented,
                                   "no measure-spec records found; cannot build a score");
    }

    Staff staff;
    staff.initial_clef = clef;
    std::optional<TimeSignature> prev_time;
    std::optional<KeySignature> prev_key;
    bool first = true;
    for (const auto& entry : measure_specs) {
        const container::OtherRecord* record = entry.second;
        const TimeSignature time = derive_time_signature(static_cast<int>(record->data[2]),
                                                         static_cast<int>(record->data[3]));
        const KeySignature key = key_from_field(record->data[1]);

        Measure measure;
        // Emit an attribute only on the first measure or when it changes.
        if (first || !prev_time || prev_time->beats != time.beats ||
            prev_time->beat_type != time.beat_type) {
            measure.time = time;
        }
        if (first || !prev_key || prev_key->fifths != key.fifths || prev_key->mode != key.mode) {
            measure.key = key;
        }
        if (first) {
            measure.clef = clef;
        }
        prev_time = time;
        prev_key = key;
        first = false;
        staff.measures.push_back(std::move(measure));
    }

    Part part;
    part.id = "P1";
    part.name = "Music";
    part.staves.push_back(std::move(staff));

    Score score;
    score.parts.push_back(std::move(part));

    diags.warn("note/entry decoding is not yet implemented; measures carry key/time/clef "
               "attributes but no notes");
    return Result<Score>::ok(std::move(score));
}

} // namespace

ir::Score build_trivial_score() {
    using namespace ir;

    // The measure: a single quarter-note middle C (C4) followed by rests that
    // fill out the 4/4 bar (half + quarter = 3072 EDU) so the measure sums to a
    // full 4096 EDU, mirroring the trailing-rest fill Finale emits.
    Voice voice;

    Note c4;
    c4.pitch = Pitch{Step::C, 4, 0};
    Entry note_entry;
    note_entry.duration = kEduQuarter;                // 1024
    note_entry.is_rest = false;
    note_entry.type = resolve_note_type(kEduQuarter); // {Quarter, 0}
    note_entry.notes.push_back(c4);
    voice.entries.push_back(std::move(note_entry));

    Entry half_rest;
    half_rest.duration = kEduHalf;                // 2048
    half_rest.is_rest = true;
    half_rest.type = resolve_note_type(kEduHalf); // {Half, 0}
    voice.entries.push_back(std::move(half_rest));

    Entry quarter_rest;
    quarter_rest.duration = kEduQuarter;                // 1024
    quarter_rest.is_rest = true;
    quarter_rest.type = resolve_note_type(kEduQuarter); // {Quarter, 0}
    voice.entries.push_back(std::move(quarter_rest));

    Measure measure;
    measure.key = KeySignature{0, KeySignature::Mode::Major}; // C major
    measure.time = TimeSignature{4, 4};                       // 4/4
    measure.clef = Clef{ClefSign::G, 2};                      // treble
    measure.voices.push_back(std::move(voice));

    Staff staff;
    staff.initial_clef = Clef{ClefSign::G, 2};
    staff.measures.push_back(std::move(measure));

    Part part;
    part.id = "P1";
    part.name = "Music";
    part.staves.push_back(std::move(staff));

    Score score;
    score.work_title = "trivial-c4-quarter";
    score.parts.push_back(std::move(part));
    return score;
}

Result<std::string> convert_mus_to_musicxml(std::span<const std::byte> data, Diagnostics& diags) {
    // Layer 1: validate the container magic / era.
    const EnigmaVersion version = detect_version(data);
    if (version.era == EnigmaEra::Unknown) {
        diags.error("unrecognized container: no known Enigma Binary File (.mus) magic found", 0);
        return Result<std::string>::fail(
            ErrorCode::BadMagic,
            "unrecognized container: no known Enigma Binary File (.mus) magic found");
    }
    diags.info(std::string("detected container era: ") + to_string(version.era), 0);
    if (!version.version_string.empty()) {
        diags.info("embedded version string: " + version.version_string, 0);
    }

    // Layer 1.5: decode the compressed "Others" pool.
    Result<std::vector<container::OtherRecord>> others = container::read_others_pool(data, diags);
    if (!others) {
        return Result<std::string>::fail(others.code(), others.message());
    }

    // Layer 2: build the IR (measure attributes today).
    Result<ir::Score> score = build_score_from_others(others.value(), diags);
    if (!score) {
        return Result<std::string>::fail(score.code(), score.message());
    }

    // Layer 3: emit MusicXML.
    return musicxml::emit_score_partwise(score.value());
}

} // namespace rescore
