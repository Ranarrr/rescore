// SPDX-License-Identifier: MIT
//
// musicxml.hpp - Layer 3, the MusicXML 4.0 emitter (namespace
// rescore::musicxml).
//
// A pure function from the clean IR to a score-partwise MusicXML 4.0 document
// string. It has ZERO knowledge of Enigma or of the binary container. The
// emitter targets the MVP subset: one or more parts/staves, per-measure
// attributes (divisions/key/time/clef), notes/rests/chords, dots, and ties.

#ifndef RESCORE_MUSICXML_HPP
#define RESCORE_MUSICXML_HPP

#include <string>

#include "rescore/ir.hpp"
#include "rescore/result.hpp"

namespace rescore::musicxml {

/// Emitter knobs. `divisions` is fixed at 1024 by default so that a MusicXML
/// <duration> equals the IR's EDU value directly (quarter = 1024). `pretty`
/// toggles indentation/newlines.
struct EmitOptions {
    bool pretty{true};
    int divisions{1024};
};

/// Emit a complete score-partwise MusicXML 4.0 document for `score`. Returns the
/// document string on success, or an Error (never throws). The output includes
/// the XML declaration and the score-partwise 4.0 DOCTYPE.
[[nodiscard]] Result<std::string> emit_score_partwise(const ir::Score& score,
                                                      const EmitOptions& options = {});

} // namespace rescore::musicxml

#endif // RESCORE_MUSICXML_HPP
