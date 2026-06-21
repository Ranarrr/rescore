// SPDX-License-Identifier: MIT
//
// ir.cpp - IR helper implementations (EDU -> NoteType resolution).

#include "rescore/ir.hpp"

#include <array>
#include <cstdlib>

namespace rescore::ir {
namespace {

struct TypeEntry {
    Edu edu;
    NoteType type;
};

// All supported (base + single-dot) durations in the MVP subset, descending by
// EDU so the nearest-match scan is deterministic. Dotted = base * 1.5.
constexpr std::array<TypeEntry, 10> kTable = {{
    {kEduWhole + kEduWhole / 2, {NoteTypeName::Whole, 1}},       // 6144 dotted whole
    {kEduWhole, {NoteTypeName::Whole, 0}},                       // 4096
    {kEduHalf + kEduHalf / 2, {NoteTypeName::Half, 1}},          // 3072 dotted half
    {kEduHalf, {NoteTypeName::Half, 0}},                         // 2048
    {kEduQuarter + kEduQuarter / 2, {NoteTypeName::Quarter, 1}}, // 1536 dotted quarter
    {kEduQuarter, {NoteTypeName::Quarter, 0}},                   // 1024
    {kEduEighth + kEduEighth / 2, {NoteTypeName::Eighth, 1}},    // 768 dotted eighth
    {kEduEighth, {NoteTypeName::Eighth, 0}},                     // 512
    {kEduSixteenth + kEduSixteenth / 2, {NoteTypeName::Sixteenth, 1}}, // 384 dotted 16th
    {kEduSixteenth, {NoteTypeName::Sixteenth, 0}},               // 256
}};

} // namespace

NoteType resolve_note_type(Edu duration) {
    // Exact match first.
    for (const auto& e : kTable) {
        if (e.edu == duration) {
            return e.type;
        }
    }

    // Otherwise pick the table entry with the smallest absolute EDU distance.
    // (Callers that care can compare resolve_note_type against the raw EDU and
    // emit a diagnostic; this function itself never fails.)
    const TypeEntry* best = &kTable[0];
    Edu best_dist = std::abs(duration - kTable[0].edu);
    for (const auto& e : kTable) {
        const Edu dist = std::abs(duration - e.edu);
        if (dist < best_dist) {
            best_dist = dist;
            best = &e;
        }
    }
    return best->type;
}

} // namespace rescore::ir
