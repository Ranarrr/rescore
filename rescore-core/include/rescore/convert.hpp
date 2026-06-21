// SPDX-License-Identifier: MIT
//
// convert.hpp - The end-to-end pipeline facade.
//
// Wires Layer 1 (container) -> Layer 2 (IR builder) -> Layer 3 (emitter) into a
// single call. For a recognized V3+ (Windows-era) container, convert_mus_to_musicxml
// decompresses the Enigma "Others" pool and emits MusicXML carrying per-measure
// key/time/clef attributes; note (entry pool) decoding is still pending. It
// returns NotImplemented only when no measure specs can be decoded (an empty or
// unsupported - e.g. big-endian Mac-era - content chunk). build_trivial_score()
// hand-constructs the canonical "trivial-c4-quarter" score (the only path that
// currently carries notes) so the emit path can be exercised end to end and
// proven to yield valid MusicXML.

#ifndef RESCORE_CONVERT_HPP
#define RESCORE_CONVERT_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "rescore/ir.hpp"
#include "rescore/result.hpp"

namespace rescore {

/// Full conversion: raw .mus bytes -> MusicXML 4.0 string. Detects the
/// container, decodes the Enigma "Others" pool, builds the IR (currently
/// measure attributes - key/time/clef - only; note decoding is pending), and
/// emits MusicXML. Returns an Error when no score can be built; diagnostics
/// carry the detail. Never throws.
[[nodiscard]] Result<std::string> convert_mus_to_musicxml(std::span<const std::byte> data,
                                                          Diagnostics& diags);

/// Hand-built canonical fixture: one part, one staff, 4/4, treble clef, C major,
/// one measure, a single quarter-note middle C (C4). Lets the CLI and tests
/// drive the emitter without any binary input.
[[nodiscard]] ir::Score build_trivial_score();

// --- Enigma measure-spec -> IR attribute mappings ---------------------------
// These translate the raw fields of a Measure Spec / Staff Spec "Other" record
// into IR values. They are pure, total functions (no failure path) so they can
// be unit-tested directly.

/// Map a Finale measure-spec (beats, divbeat) pair to a displayed time
/// signature. `divbeat` is EDU-per-beat: an undotted note value yields a simple
/// meter (denominator = whole-note / divbeat), a dotted value yields a compound
/// meter (each beat subdivides into three). Unrecognized values fall back to
/// {beats, 4}.
[[nodiscard]] ir::TimeSignature derive_time_signature(int beats, int divbeat);

/// Map a Finale clef index to an IR clef (0=treble, 3=bass, 1=alto, 2=tenor;
/// anything else falls back to treble).
[[nodiscard]] ir::Clef clef_from_index(int clef_index);

/// Map a Finale measure-spec key field to an IR key signature: the low byte is
/// the signed circle-of-fifths accidental count, bits 13-8 are the mode bank
/// (1 = minor).
[[nodiscard]] ir::KeySignature key_from_field(std::uint16_t key_field);

} // namespace rescore

#endif // RESCORE_CONVERT_HPP
