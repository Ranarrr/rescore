// tests/ir_duration_test.cpp
//
// Unit tests for the Layer 2 EDU -> note-type resolver (rescore::ir).
//
// Finale measures duration in EDU ticks. With quarter = 1024:
//   whole = 4096, half = 2048, quarter = 1024, eighth = 512, sixteenth = 256.
// One augmentation dot multiplies by 1.5, so a dotted quarter = 1536.
// resolve_note_type maps an Edu value to a {NoteTypeName, dots} pair; this is
// pure logic with no binary and no XML, exactly the kind of thing the IR layer
// must be unit-testable for.

#include <rescore/ir.hpp>

#include <catch2/catch_test_macros.hpp>

using rescore::ir::Edu;
using rescore::ir::NoteType;
using rescore::ir::NoteTypeName;
using rescore::ir::resolve_note_type;

TEST_CASE("resolve_note_type: undotted base durations", "[ir][duration]") {
    SECTION("whole = 4096") {
        NoteType t = resolve_note_type(Edu{4096});
        REQUIRE(t.name == NoteTypeName::Whole);
        REQUIRE(t.dots == 0);
    }
    SECTION("half = 2048") {
        NoteType t = resolve_note_type(Edu{2048});
        REQUIRE(t.name == NoteTypeName::Half);
        REQUIRE(t.dots == 0);
    }
    SECTION("quarter = 1024") {
        NoteType t = resolve_note_type(Edu{1024});
        REQUIRE(t.name == NoteTypeName::Quarter);
        REQUIRE(t.dots == 0);
    }
    SECTION("eighth = 512") {
        NoteType t = resolve_note_type(Edu{512});
        REQUIRE(t.name == NoteTypeName::Eighth);
        REQUIRE(t.dots == 0);
    }
    SECTION("sixteenth = 256") {
        NoteType t = resolve_note_type(Edu{256});
        REQUIRE(t.name == NoteTypeName::Sixteenth);
        REQUIRE(t.dots == 0);
    }
}

TEST_CASE("resolve_note_type: dotted quarter = 1536", "[ir][duration]") {
    // 1024 * 1.5 == 1536: the base note is a quarter, carrying one dot.
    NoteType t = resolve_note_type(Edu{1536});
    REQUIRE(t.name == NoteTypeName::Quarter);
    REQUIRE(t.dots == 1);
}
