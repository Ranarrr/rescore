// tests/entries_test.cpp
//
// Tests for the note "entry" pool reader (rescore::container::read_entry_pool)
// and the end-to-end note conversion. The entry chunk is LZSS-compressed and
// decodes to fixed 38-byte records carrying notes, chords, and rests; sequential
// duration accumulation places them across measures. These cases read committed
// .mus fixtures from tests/corpus/re (path injected via RESCORE_CORPUS_RE_DIR);
// they SKIP, rather than fail, if a fixture is unavailable.

#include <rescore/convert.hpp>
#include <rescore/entries.hpp>
#include <rescore/result.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using rescore::container::EntryRecord;
using rescore::container::read_entry_pool;

namespace {

#ifndef RESCORE_CORPUS_RE_DIR
#define RESCORE_CORPUS_RE_DIR "."
#endif

std::optional<std::vector<std::byte>> load_fixture(const std::string& name) {
    const std::string path = std::string(RESCORE_CORPUS_RE_DIR) + "/" + name;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char c : raw) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return bytes;
}

/// The note-bearing entries (note-count > 0), in order, dropping rest entries.
std::vector<EntryRecord> notes_only(const std::vector<EntryRecord>& entries) {
    std::vector<EntryRecord> out;
    for (const auto& e : entries) {
        if (!e.notes.empty()) {
            out.push_back(e);
        }
    }
    return out;
}

} // namespace

TEST_CASE("entries: a single G4 quarter note decodes", "[entries]") {
    const auto buf = load_fixture("note-g4-quarter.mus");
    if (!buf) {
        SKIP("fixture note-g4-quarter.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = read_entry_pool(*buf, diags);
    REQUIRE(res.has_value());
    const auto notes = notes_only(res.value());
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].notes.size() == 1);
    CHECK(notes[0].duration_edu == 1024);       // quarter
    CHECK(notes[0].notes[0].step_from_c4 == 4); // G4 = C,D,E,F,G
    CHECK(notes[0].notes[0].alter == 0);
}

TEST_CASE("entries: a Finale 2011 (zlib) file decodes its entry pool", "[entries][zlib]") {
    const auto buf = load_fixture("Tomkins_-_Out_of_the_deep.mus");
    if (!buf) {
        SKIP("fixture Tomkins_-_Out_of_the_deep.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = read_entry_pool(*buf, diags);
    REQUIRE(res.has_value());
    // This Finale 2011 file's entry pool has 571 records (ids 1..572); decoding
    // them at all proves the zlib inflater + the type-22 entry path.
    CHECK(res.value().size() == 571);
}

TEST_CASE("entries: an ascending quarter run gives four stepwise pitches", "[entries]") {
    const auto buf = load_fixture("run-cdef-q.mus");
    if (!buf) {
        SKIP("fixture run-cdef-q.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = read_entry_pool(*buf, diags);
    REQUIRE(res.has_value());
    const auto notes = notes_only(res.value());
    REQUIRE(notes.size() == 4);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        REQUIRE(notes[i].notes.size() == 1);
        CHECK(notes[i].duration_edu == 1024);
    }
    const int base = notes[0].notes[0].step_from_c4;
    CHECK(notes[1].notes[0].step_from_c4 == base + 1);
    CHECK(notes[2].notes[0].step_from_c4 == base + 2);
    CHECK(notes[3].notes[0].step_from_c4 == base + 3);
}

TEST_CASE("entries: a chord is one entry with stacked pitches", "[entries][chord]") {
    const auto buf = load_fixture("chord-ceg.mus");
    if (!buf) {
        SKIP("fixture chord-ceg.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = read_entry_pool(*buf, diags);
    REQUIRE(res.has_value());
    const auto notes = notes_only(res.value());
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].notes.size() == 3); // a triad: one rhythmic entry, three pitches
    const int base = notes[0].notes[0].step_from_c4;
    CHECK(notes[0].notes[1].step_from_c4 == base + 2); // stacked in thirds
    CHECK(notes[0].notes[2].step_from_c4 == base + 4);
}

TEST_CASE("entries: sharp and flat alterations decode", "[entries][accidental]") {
    if (const auto s = load_fixture("note-fsharp4-whole.mus")) {
        rescore::Diagnostics diags;
        const auto res = read_entry_pool(*s, diags);
        REQUIRE(res.has_value());
        const auto notes = notes_only(res.value());
        REQUIRE(notes.size() == 1);
        REQUIRE(notes[0].notes.size() == 1);
        CHECK(notes[0].notes[0].alter == 1); // sharp
    }
    if (const auto f = load_fixture("note-fflat4-whole.mus")) {
        rescore::Diagnostics diags;
        const auto res = read_entry_pool(*f, diags);
        REQUIRE(res.has_value());
        const auto notes = notes_only(res.value());
        REQUIRE(notes.size() == 1);
        REQUIRE(notes[0].notes.size() == 1);
        CHECK(notes[0].notes[0].alter == -1); // flat
    }
}

TEST_CASE("convert: two-measures places a note in bar 1 and a note in bar 2", "[convert][entries]") {
    const auto buf = load_fixture("two-measures.mus");
    if (!buf) {
        SKIP("fixture two-measures.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<measure number=\"1\""));
    REQUIRE_THAT(xml, ContainsSubstring("<measure number=\"2\""));
    REQUIRE_THAT(xml, ContainsSubstring("<step>")); // at least one pitched note
}

TEST_CASE("convert: a chord emits a MusicXML <chord/>", "[convert][chord]") {
    const auto buf = load_fixture("chord-ceg.mus");
    if (!buf) {
        SKIP("fixture chord-ceg.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    REQUIRE_THAT(res.value(), ContainsSubstring("<chord/>"));
}

TEST_CASE("convert: a two-staff piano yields two parts with treble and bass clefs",
          "[convert][multistaff]") {
    const auto buf = load_fixture("piano-both.mus");
    if (!buf) {
        SKIP("fixture piano-both.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<part id=\"P1\""));
    REQUIRE_THAT(xml, ContainsSubstring("<part id=\"P2\""));
    // The lower staff carries a bass clef (F clef on line 4).
    REQUIRE_THAT(xml, ContainsSubstring("<sign>F</sign>"));
    REQUIRE_THAT(xml, ContainsSubstring("<line>4</line>"));
}

TEST_CASE("convert: tied notes emit tie start and stop", "[convert][tie]") {
    const auto buf = load_fixture("tie.mus");
    if (!buf) {
        SKIP("fixture tie.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<tie type=\"start\"/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<tie type=\"stop\"/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<tied type=\"start\"/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<tied type=\"stop\"/>"));
}

TEST_CASE("convert: a triplet emits time-modification and tuplet brackets",
          "[convert][tuplet]") {
    const auto buf = load_fixture("triplet.mus");
    if (!buf) {
        SKIP("fixture triplet.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<actual-notes>3</actual-notes>"));
    REQUIRE_THAT(xml, ContainsSubstring("<normal-notes>2</normal-notes>"));
    REQUIRE_THAT(xml, ContainsSubstring("<tuplet type=\"start\"/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<tuplet type=\"stop\"/>"));
}

TEST_CASE("convert: two voices on one staff emit voice numbers and a backup",
          "[convert][voices]") {
    const auto buf = load_fixture("voices.mus");
    if (!buf) {
        SKIP("fixture voices.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<voice>1</voice>"));
    REQUIRE_THAT(xml, ContainsSubstring("<voice>2</voice>"));
    REQUIRE_THAT(xml, ContainsSubstring("<backup>"));
}

TEST_CASE("convert: a slur emits start and stop on its endpoint notes", "[convert][slur]") {
    const auto buf = load_fixture("slur.mus");
    if (!buf) {
        SKIP("fixture slur.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<slur type=\"start\" number=\"1\"/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<slur type=\"stop\" number=\"1\"/>"));
}

TEST_CASE("convert: articulations emit staccato / accent / tenuto and a fermata",
          "[convert][artic]") {
    const auto buf = load_fixture("artic.mus");
    if (!buf) {
        SKIP("fixture artic.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<articulations>"));
    REQUIRE_THAT(xml, ContainsSubstring("<staccato/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<accent/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<tenuto/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<fermata/>"));
}

TEST_CASE("convert: lyrics emit hyphenated syllables under their notes", "[convert][lyrics]") {
    const auto buf = load_fixture("lyrics.mus");
    if (!buf) {
        SKIP("fixture lyrics.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    // "A-ve-Ma-ri" hyphenated across four notes (text decoded from the method-1
    // Text pool): begin / middle / middle / end.
    REQUIRE_THAT(xml, ContainsSubstring("<lyric number=\"1\">"));
    REQUIRE_THAT(xml, ContainsSubstring("<syllabic>begin</syllabic>"));
    REQUIRE_THAT(xml, ContainsSubstring("<text>A</text>"));
    REQUIRE_THAT(xml, ContainsSubstring("<syllabic>end</syllabic>"));
    REQUIRE_THAT(xml, ContainsSubstring("<text>ri</text>"));
}

TEST_CASE("convert: dynamics emit f / p marks and a crescendo wedge", "[convert][dynamics]") {
    const auto buf = load_fixture("dynamics.mus");
    if (!buf) {
        SKIP("fixture dynamics.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    REQUIRE_THAT(xml, ContainsSubstring("<dynamics>"));
    REQUIRE_THAT(xml, ContainsSubstring("<f/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<p/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<wedge type=\"crescendo\"/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<wedge type=\"stop\"/>"));
}

TEST_CASE("convert: library dynamics map their music-font glyphs", "[convert][dynamics]") {
    const auto buf = load_fixture("dyn-marks.mus");
    if (!buf) {
        SKIP("fixture dyn-marks.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    // The eight library dynamics are stored as single Maestro glyphs, not ASCII.
    REQUIRE_THAT(xml, ContainsSubstring("<pp/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<mp/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<mf/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<ff/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<fp/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<sf/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<sfz/>"));
    REQUIRE_THAT(xml, ContainsSubstring("<fz/>"));
}

TEST_CASE("convert: a per-staff dynamic routes to its own part", "[convert][dynamics]") {
    const auto buf = load_fixture("dyn-staff.mus");
    if (!buf) {
        SKIP("fixture dyn-staff.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    // f is on the treble staff (part P1), p on the bass staff (part P2).
    const auto p2 = xml.find("<part id=\"P2\"");
    const auto f = xml.find("<f/>");
    const auto p = xml.find("<p/>");
    REQUIRE(p2 != std::string::npos);
    REQUIRE(f != std::string::npos);
    REQUIRE(p != std::string::npos);
    CHECK(f < p2); // forte stays in the first part
    CHECK(p > p2); // piano lands in the second part
}

TEST_CASE("convert: describe_era identifies the Finale build and format generation",
          "[convert][era]") {
    if (const auto t = load_fixture("Tomkins_-_Out_of_the_deep.mus")) {
        const std::string era = rescore::describe_era(*t);
        REQUIRE_THAT(era, ContainsSubstring("late-era"));
        REQUIRE_THAT(era, ContainsSubstring("2011"));
    }
    if (const auto s = load_fixture("Scapulis.MUS")) {
        const std::string era = rescore::describe_era(*s);
        REQUIRE_THAT(era, ContainsSubstring("2003-era"));
        REQUIRE_THAT(era, ContainsSubstring("2002"));
    }
    if (const auto a = load_fixture("Tye_-_Alleluia.mus")) {
        const std::string era = rescore::describe_era(*a);
        REQUIRE_THAT(era, ContainsSubstring("late-era"));
        REQUIRE_THAT(era, ContainsSubstring("2012"));
    }
    // Random bytes are reported as unrecognized, never crash.
    const std::vector<std::byte> junk(64, std::byte{0x5A});
    REQUIRE_THAT(rescore::describe_era(junk), ContainsSubstring("unrecognized"));
}

TEST_CASE("convert: a Finale 2011 (zlib) file converts to MusicXML with notes",
          "[convert][zlib]") {
    const auto buf = load_fixture("Tomkins_-_Out_of_the_deep.mus");
    if (!buf) {
        SKIP("fixture Tomkins_-_Out_of_the_deep.mus not available");
    }
    rescore::Diagnostics diags;
    const auto res = rescore::convert_mus_to_musicxml(*buf, diags);
    REQUIRE(res.has_value());
    const std::string& xml = res.value();
    // The late-era zlib path builds one part per entry-chain head; this anthem has
    // four active voices and real pitched notes survive the conversion.
    REQUIRE_THAT(xml, ContainsSubstring("<part id=\"P1\""));
    REQUIRE_THAT(xml, ContainsSubstring("<part id=\"P4\""));
    REQUIRE_THAT(xml, ContainsSubstring("<step>"));
    // The work title is pulled from the first page-text block of the text pool.
    REQUIRE_THAT(xml, ContainsSubstring("<work-title>OUT OF THE DEEP</work-title>"));
    // Per-staff clefs come from the type-26 Staff records: the Tenor voice is a
    // vocal treble_8 (octave change -1), which the pitch heuristic cannot produce.
    REQUIRE_THAT(xml, ContainsSubstring("<clef-octave-change>-1</clef-octave-change>"));
    // Verse lyrics are attached per note from the type-27 class 0x454 records.
    REQUIRE_THAT(xml, ContainsSubstring("<text>Out</text>"));
    REQUIRE_THAT(xml, ContainsSubstring("<text>deep</text>"));
}
