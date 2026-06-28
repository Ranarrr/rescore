// tests/emit_trivial_test.cpp
//
// End-to-end Layer 2 -> Layer 3 test for the canonical trivial gold score:
// one Part, one Staff, 4/4, treble clef (G line 2), C-major key (fifths 0),
// one measure, a single quarter-note middle C (C4).
//
// We assert on MUSICAL FACTS present in the emitted MusicXML 4.0 string, not on
// byte-equality to a gold file. The brief warns that naive XML diffs drown you
// in cosmetic false positives (attribute order, whitespace, optional defaults),
// so these are targeted semantic substring checks: the right step, octave,
// key, time, clef, note type, and the divisions/duration relationship that
// makes <duration> equal the EDU value directly.

#include <rescore/convert.hpp>  // build_trivial_score
#include <rescore/ir.hpp>
#include <rescore/musicxml.hpp> // emit_score_partwise
#include <rescore/result.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

std::string emit_trivial() {
    const rescore::ir::Score score = rescore::build_trivial_score();
    rescore::Result<std::string> emitted = rescore::musicxml::emit_score_partwise(score);
    REQUIRE(emitted.has_value());
    return emitted.value();
}

} // namespace

TEST_CASE("emit trivial: document header is present", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    // Must begin with the XML declaration.
    REQUIRE_THAT(xml, StartsWith("<?xml"));
    // A score-partwise DOCTYPE anchors it as MusicXML partwise content.
    REQUIRE_THAT(xml, ContainsSubstring("<!DOCTYPE score-partwise"));
    // The root element declares MusicXML version 4.0.
    REQUIRE_THAT(xml, ContainsSubstring("<score-partwise"));
    REQUIRE_THAT(xml, ContainsSubstring("version=\"4.0\""));
    // Encoding provenance identifies the producing software.
    REQUIRE_THAT(xml, ContainsSubstring("<software>Rescore</software>"));
}

TEST_CASE("emit trivial: pitch is middle C (C4)", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    REQUIRE_THAT(xml, ContainsSubstring("<step>C</step>"));
    REQUIRE_THAT(xml, ContainsSubstring("<octave>4</octave>"));
}

TEST_CASE("emit trivial: key signature is C major (fifths 0)", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    REQUIRE_THAT(xml, ContainsSubstring("<fifths>0</fifths>"));
}

TEST_CASE("emit trivial: time signature is 4/4", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    REQUIRE_THAT(xml, ContainsSubstring("<beats>4</beats>"));
    REQUIRE_THAT(xml, ContainsSubstring("<beat-type>4</beat-type>"));
}

TEST_CASE("emit trivial: clef is treble (G line 2)", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    REQUIRE_THAT(xml, ContainsSubstring("<sign>G</sign>"));
    REQUIRE_THAT(xml, ContainsSubstring("<line>2</line>"));
}

TEST_CASE("emit trivial: note type is quarter", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    REQUIRE_THAT(xml, ContainsSubstring("<type>quarter</type>"));
}

TEST_CASE("emit trivial: divisions=1024 makes duration the EDU value", "[emit][musicxml]") {
    const std::string xml = emit_trivial();
    // With divisions == 1024, a quarter note's <duration> EQUALS its EDU value.
    REQUIRE_THAT(xml, ContainsSubstring("<divisions>1024</divisions>"));
    REQUIRE_THAT(xml, ContainsSubstring("<duration>1024</duration>"));
}
