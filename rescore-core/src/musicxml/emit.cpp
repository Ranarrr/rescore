// SPDX-License-Identifier: MIT
//
// emit.cpp - Hand-rolled MusicXML 4.0 score-partwise emitter for the MVP
// subset. No XML library: we build the document as a string with careful
// escaping. The output validates against the official MusicXML 4.0 XSD for the
// supported element set and opens cleanly in MuseScore.

#include "rescore/musicxml.hpp"

#include <cstddef>
#include <string>

namespace rescore::musicxml {
namespace {

/// Minimal indenting string builder. When `pretty` is false it emits no
/// indentation or newlines (still a single well-formed line-free document).
class XmlBuilder {
public:
    explicit XmlBuilder(bool pretty) : pretty_(pretty) {}

    void open(const std::string& tag) {
        indent();
        out_ += '<';
        out_ += tag;
        out_ += '>';
        newline();
        ++depth_;
    }

    void close(const std::string& tag) {
        --depth_;
        indent();
        out_ += "</";
        out_ += tag;
        out_ += '>';
        newline();
    }

    /// <tag>text</tag> with text escaped, on one logical line.
    void leaf(const std::string& tag, const std::string& text) {
        indent();
        out_ += '<';
        out_ += tag;
        out_ += '>';
        out_ += escape(text);
        out_ += "</";
        out_ += tag;
        out_ += '>';
        newline();
    }

    /// Self-closing <tag/>.
    void empty(const std::string& tag) {
        indent();
        out_ += '<';
        out_ += tag;
        out_ += "/>";
        newline();
    }

    /// Open tag carrying attributes: <tag a="b" c="d">.
    void open_attr(const std::string& tag, const std::string& attrs) {
        indent();
        out_ += '<';
        out_ += tag;
        out_ += ' ';
        out_ += attrs;
        out_ += '>';
        newline();
        ++depth_;
    }

    /// Self-closing tag carrying attributes: <tag a="b"/>.
    void empty_attr(const std::string& tag, const std::string& attrs) {
        indent();
        out_ += '<';
        out_ += tag;
        out_ += ' ';
        out_ += attrs;
        out_ += "/>";
        newline();
    }

    void raw_line(const std::string& line) {
        out_ += line;
        newline();
    }

    [[nodiscard]] std::string take() { return std::move(out_); }

    /// Escape the five XML predefined entities for element text / attributes.
    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

private:
    void indent() {
        if (pretty_) {
            out_.append(static_cast<std::size_t>(depth_) * 2, ' ');
        }
    }
    void newline() {
        if (pretty_) {
            out_ += '\n';
        }
    }

    bool pretty_;
    int depth_{0};
    std::string out_;
};

[[nodiscard]] const char* step_name(ir::Step s) {
    switch (s) {
    case ir::Step::C:
        return "C";
    case ir::Step::D:
        return "D";
    case ir::Step::E:
        return "E";
    case ir::Step::F:
        return "F";
    case ir::Step::G:
        return "G";
    case ir::Step::A:
        return "A";
    case ir::Step::B:
        return "B";
    }
    return "C";
}

[[nodiscard]] const char* clef_sign_name(ir::ClefSign s) {
    switch (s) {
    case ir::ClefSign::G:
        return "G";
    case ir::ClefSign::F:
        return "F";
    case ir::ClefSign::C:
        return "C";
    case ir::ClefSign::Percussion:
        return "percussion";
    }
    return "G";
}

[[nodiscard]] const char* note_type_name(ir::NoteTypeName n) {
    switch (n) {
    case ir::NoteTypeName::Whole:
        return "whole";
    case ir::NoteTypeName::Half:
        return "half";
    case ir::NoteTypeName::Quarter:
        return "quarter";
    case ir::NoteTypeName::Eighth:
        return "eighth";
    case ir::NoteTypeName::Sixteenth:
        return "16th";
    }
    return "quarter";
}

[[nodiscard]] const char* mode_name(ir::KeySignature::Mode m) {
    switch (m) {
    case ir::KeySignature::Mode::Major:
        return "major";
    case ir::KeySignature::Mode::Minor:
        return "minor";
    case ir::KeySignature::Mode::None:
        return "none";
    }
    return "major";
}

/// Scale an EDU duration to MusicXML <duration> units given `divisions` per
/// quarter. With divisions=1024 this is the identity (EDU == duration), which is
/// the intended configuration.
[[nodiscard]] int edu_to_duration(ir::Edu edu, int divisions) {
    // <duration> = edu * divisions / kEduPerQuarter. Use 64-bit intermediate to
    // avoid overflow on long durations.
    const long long scaled = static_cast<long long>(edu) * divisions / ir::kEduPerQuarter;
    return static_cast<int>(scaled);
}

void emit_attributes(XmlBuilder& b, const ir::Measure& measure, const ir::Clef& fallback_clef,
                     int divisions) {
    b.open("attributes");
    b.leaf("divisions", std::to_string(divisions));

    if (measure.key) {
        b.open("key");
        b.leaf("fifths", std::to_string(measure.key->fifths));
        b.leaf("mode", mode_name(measure.key->mode));
        b.close("key");
    }

    if (measure.time) {
        b.open("time");
        b.leaf("beats", std::to_string(measure.time->beats));
        b.leaf("beat-type", std::to_string(measure.time->beat_type));
        b.close("time");
    }

    // Always emit a clef on the first measure (use the explicit one if present,
    // otherwise the staff's initial clef passed in as fallback).
    const ir::Clef clef = measure.clef ? *measure.clef : fallback_clef;
    b.open("clef");
    b.leaf("sign", clef_sign_name(clef.sign));
    b.leaf("line", std::to_string(clef.line));
    b.close("clef");

    b.close("attributes");
}

void emit_note(XmlBuilder& b, const ir::Entry& entry, const ir::Note& note, bool is_chord_member,
               int divisions) {
    b.open("note");

    if (is_chord_member) {
        b.empty("chord");
    }

    if (entry.is_rest) {
        b.empty("rest");
    } else {
        b.open("pitch");
        b.leaf("step", step_name(note.pitch.step));
        if (note.pitch.alter != 0) {
            b.leaf("alter", std::to_string(note.pitch.alter));
        }
        b.leaf("octave", std::to_string(note.pitch.octave));
        b.close("pitch");
    }

    b.leaf("duration", std::to_string(edu_to_duration(entry.duration, divisions)));

    // <tie> elements are the sounding (sound-level) ties; they must precede the
    // <type> element in the note content order.
    if (!entry.is_rest) {
        if (note.tie_stop) {
            b.empty_attr("tie", "type=\"stop\"");
        }
        if (note.tie_start) {
            b.empty_attr("tie", "type=\"start\"");
        }
    }

    b.leaf("type", note_type_name(entry.type.name));
    for (int d = 0; d < entry.type.dots; ++d) {
        b.empty("dot");
    }

    // <notations><tied> are the notated (visual) ties.
    if (!entry.is_rest && (note.tie_start || note.tie_stop)) {
        b.open("notations");
        if (note.tie_stop) {
            b.empty_attr("tied", "type=\"stop\"");
        }
        if (note.tie_start) {
            b.empty_attr("tied", "type=\"start\"");
        }
        b.close("notations");
    }

    b.close("note");
}

void emit_measure(XmlBuilder& b, const ir::Measure& measure, const ir::Clef& fallback_clef,
                  int measure_number, bool emit_attrs, int divisions) {
    b.open_attr("measure", "number=\"" + std::to_string(measure_number) + "\"");

    if (emit_attrs) {
        emit_attributes(b, measure, fallback_clef, divisions);
    }

    // MVP: a single voice per staff. If multiple voices are present we still
    // emit them in sequence (the IR keeps each voice's entries ordered); full
    // multi-voice backup/forward handling is deferred per the MVP scope.
    for (const auto& voice : measure.voices) {
        for (const auto& entry : voice.entries) {
            if (entry.is_rest || entry.notes.empty()) {
                // A rest is emitted as a single <note><rest/>...; build a
                // synthetic single-note iteration.
                ir::Note placeholder{};
                emit_note(b, entry, placeholder, /*is_chord_member=*/false, divisions);
            } else {
                for (std::size_t i = 0; i < entry.notes.size(); ++i) {
                    emit_note(b, entry, entry.notes[i], /*is_chord_member=*/i > 0, divisions);
                }
            }
        }
    }

    b.close("measure");
}

} // namespace

Result<std::string> emit_score_partwise(const ir::Score& score, const EmitOptions& options) {
    if (options.divisions <= 0) {
        return Result<std::string>::fail(ErrorCode::InvalidArgument,
                                         "divisions must be positive");
    }
    if (score.parts.empty()) {
        return Result<std::string>::fail(ErrorCode::EmitFailure,
                                         "score has no parts; nothing to emit");
    }

    XmlBuilder b(options.pretty);

    // Prologue: XML declaration + DOCTYPE for MusicXML 4.0 score-partwise.
    b.raw_line(R"(<?xml version="1.0" encoding="UTF-8" standalone="no"?>)");
    b.raw_line(R"(<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 )"
               R"(Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">)");

    b.open_attr("score-partwise", "version=\"4.0\"");

    // work / movement title.
    if (!score.work_title.empty()) {
        b.open("work");
        b.leaf("work-title", score.work_title);
        b.close("work");
    }

    // part-list.
    b.open("part-list");
    for (const auto& part : score.parts) {
        b.open_attr("score-part", "id=\"" + XmlBuilder::escape(part.id) + "\"");
        b.leaf("part-name", part.name);
        b.close("score-part");
    }
    b.close("part-list");

    // parts.
    for (const auto& part : score.parts) {
        b.open_attr("part", "id=\"" + XmlBuilder::escape(part.id) + "\"");

        // MVP: emit the first staff. (Multi-staff/multi-part-per-instrument is
        // deferred; the IR can carry several staves but the MVP emitter renders
        // the first one as the part's measures.)
        if (!part.staves.empty()) {
            const ir::Staff& staff = part.staves.front();
            int measure_number = 1;
            for (std::size_t m = 0; m < staff.measures.size(); ++m) {
                const bool emit_attrs = (m == 0); // attributes on the first measure
                emit_measure(b, staff.measures[m], staff.initial_clef, measure_number,
                             emit_attrs, options.divisions);
                ++measure_number;
            }
        }

        b.close("part");
    }

    b.close("score-partwise");
    return Result<std::string>::ok(b.take());
}

} // namespace rescore::musicxml
