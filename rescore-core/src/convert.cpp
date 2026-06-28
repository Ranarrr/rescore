// SPDX-License-Identifier: MIT
//
// convert.cpp - End-to-end pipeline facade, the IR attribute mappings, and the
// canonical trivial-score fixture.

#include "rescore/convert.hpp"

#include "rescore/entries.hpp"
#include "rescore/musicxml.hpp"
#include "rescore/others.hpp"
#include "rescore/version.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
        return ir::Clef{ir::ClefSign::G, 2}; // treble (verified)
    case 3:
        return ir::Clef{ir::ClefSign::F, 4}; // bass (verified)
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

/// Map a decoded entry note to an IR pitch (step_from_c4 = 0 is middle C, C4).
[[nodiscard]] ir::Pitch pitch_from_entry(const container::EntryNote& n) {
    int idx = n.step_from_c4 % 7;
    int oct = 4 + n.step_from_c4 / 7;
    if (idx < 0) {
        idx += 7;
        --oct;
    }
    static constexpr ir::Step kSteps[7] = {ir::Step::C, ir::Step::D, ir::Step::E, ir::Step::F,
                                           ir::Step::G, ir::Step::A, ir::Step::B};
    return ir::Pitch{kSteps[idx], oct, n.alter};
}

/// Total EDU a measure holds under a given meter (4/4 -> 4096).
[[nodiscard]] ir::Edu measure_capacity(const ir::TimeSignature& t) {
    if (t.beats <= 0 || t.beat_type <= 0) {
        return ir::kEduWhole;
    }
    return static_cast<ir::Edu>(t.beats) * (ir::kEduWhole / t.beat_type);
}

/// Greedily fill `edu` of remaining measure time with standard rests.
void fill_rests(ir::Voice& voice, ir::Edu edu) {
    constexpr ir::Edu kValues[] = {ir::kEduWhole, ir::kEduHalf, ir::kEduQuarter, ir::kEduEighth,
                                   ir::kEduSixteenth};
    for (const ir::Edu v : kValues) {
        while (edu >= v) {
            ir::Entry rest;
            rest.duration = v;
            rest.is_rest = true;
            rest.type = ir::resolve_note_type(v);
            voice.entries.push_back(std::move(rest));
            edu -= v;
        }
    }
}

/// Build the IR notes for one raw entry into `dst`.
void fill_entry_notes(const container::EntryRecord& src, ir::Entry& dst) {
    dst.is_rest = src.notes.empty();
    for (const container::EntryNote& n : src.notes) {
        dst.notes.push_back(ir::Note{pitch_from_entry(n), n.tie_start, n.tie_stop});
    }
}

/// Slur endpoints landing on an entry (by entry id).
struct SlurMark {
    int start{0};
    int stop{0};
};

/// Per-entry feature attachments, keyed by entry id. Threaded into entry
/// resolution so each can be set on the matching ir::Entry, which would otherwise
/// have lost the raw entry id.
struct EntryFeatures {
    std::map<std::uint16_t, SlurMark> slur;
    std::map<std::uint16_t, std::vector<ir::Articulation>> artic;
    std::map<std::uint16_t, std::vector<ir::Lyric>> lyric;
};

/// Copy any decoded features for `entry_id` onto the resolved IR entry.
void attach_entry_features(ir::Entry& dst, std::uint16_t entry_id, const EntryFeatures& feats) {
    if (const auto it = feats.slur.find(entry_id); it != feats.slur.end()) {
        dst.slur_start = it->second.start;
        dst.slur_stop = it->second.stop;
    }
    if (const auto it = feats.artic.find(entry_id); it != feats.artic.end()) {
        dst.articulations = it->second;
    }
    if (const auto it = feats.lyric.find(entry_id); it != feats.lyric.end()) {
        dst.lyrics = it->second;
    }
}

/// Resolve a staff's raw entries to IR entries, expanding tuplet groups. A run of
/// in-tuplet entries shares the ratio N : 2^floor(log2 N), keeps its displayed
/// note value for `type`, and gets its sounding `duration` scaled accordingly so
/// the group occupies the right amount of measure time.
[[nodiscard]] std::vector<ir::Entry>
resolve_entries(const std::vector<container::EntryRecord>& entries, const EntryFeatures& feats) {
    std::vector<ir::Entry> out;
    out.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size();) {
        if (!entries[i].in_tuplet) {
            ir::Entry entry;
            entry.duration = entries[i].duration_edu;
            entry.type = ir::resolve_note_type(entries[i].duration_edu);
            fill_entry_notes(entries[i], entry);
            attach_entry_features(entry, entries[i].id, feats);
            out.push_back(std::move(entry));
            ++i;
            continue;
        }
        // Tuplet group [i, j): consecutive in-tuplet entries, ratio actual:normal.
        std::size_t j = i;
        while (j < entries.size() && entries[j].in_tuplet) {
            ++j;
        }
        const int actual = static_cast<int>(j - i);
        int normal = 1;
        while (normal * 2 <= actual) {
            normal *= 2;
        }
        ir::Edu total_disp = 0;
        for (std::size_t k = i; k < j; ++k) {
            total_disp += entries[k].duration_edu;
        }
        const ir::Edu total_actual =
            static_cast<ir::Edu>(static_cast<long long>(total_disp) * normal / actual);
        ir::Edu assigned = 0;
        for (std::size_t k = i; k < j; ++k) {
            ir::Entry entry;
            entry.type = ir::resolve_note_type(entries[k].duration_edu); // displayed value
            entry.time_mod = ir::TimeModification{actual, normal};
            entry.tuplet_start = (k == i);
            entry.tuplet_stop = (k + 1 == j);
            if (k + 1 == j) {
                entry.duration = total_actual - assigned; // last note absorbs rounding
            } else {
                entry.duration = static_cast<ir::Edu>(
                    static_cast<long long>(entries[k].duration_edu) * normal / actual);
                assigned += entry.duration;
            }
            fill_entry_notes(entries[k], entry);
            attach_entry_features(entry, entries[k].id, feats);
            out.push_back(std::move(entry));
        }
        i = j;
    }
    return out;
}

/// Place one layer's resolved entries into a staff as a NEW voice: build a voice
/// per measure (entries placed in time order, advancing when a measure fills, then
/// padded to capacity with rests) and append it to each measure. Called once per
/// layer so a staff can carry several voices.
void place_voice(ir::Staff& staff, const std::vector<container::EntryRecord>& entries,
                 const EntryFeatures& feats, Diagnostics& diags) {
    if (staff.measures.empty()) {
        return;
    }
    ir::TimeSignature cur_time{4, 4};
    std::vector<ir::Edu> cap(staff.measures.size());
    for (std::size_t i = 0; i < staff.measures.size(); ++i) {
        if (staff.measures[i].time) {
            cur_time = *staff.measures[i].time;
        }
        cap[i] = measure_capacity(cur_time);
    }

    std::vector<ir::Voice> voice(staff.measures.size());
    std::vector<ir::Entry> resolved = resolve_entries(entries, feats);
    std::size_t mi = 0;
    ir::Edu acc = 0;
    for (ir::Entry& entry : resolved) {
        if (mi >= voice.size()) {
            diags.warn("more note time than measures; extra notes dropped");
            break;
        }
        if (acc > 0 && acc + entry.duration > cap[mi]) {
            fill_rests(voice[mi], cap[mi] - acc);
            ++mi;
            acc = 0;
            if (mi >= voice.size()) {
                diags.warn("more note time than measures; extra notes dropped");
                break;
            }
        }
        acc += entry.duration;
        voice[mi].entries.push_back(std::move(entry));
    }
    if (mi < voice.size()) {
        fill_rests(voice[mi], cap[mi] - acc);
        ++mi;
    }
    for (; mi < voice.size(); ++mi) {
        fill_rests(voice[mi], cap[mi]);
    }

    for (std::size_t i = 0; i < staff.measures.size(); ++i) {
        staff.measures[i].voices.push_back(std::move(voice[i]));
    }
}

/// Build the per-measure attribute skeleton (key / time / clef) shared by every
/// staff, with an attribute set only on the first measure or where it changes.
[[nodiscard]] ir::Staff
build_measure_skeleton(const std::map<std::uint16_t, const container::OtherRecord*>& measure_specs,
                       const ir::Clef& clef) {
    using namespace ir;
    Staff staff;
    staff.initial_clef = clef;
    std::optional<TimeSignature> prev_time;
    std::optional<KeySignature> prev_key;
    bool first = true;
    for (const auto& ms_entry : measure_specs) {
        const container::OtherRecord* ms = ms_entry.second;
        const TimeSignature time = derive_time_signature(static_cast<int>(ms->data[2]),
                                                         static_cast<int>(ms->data[3]));
        const KeySignature key = key_from_field(ms->data[1]);
        Measure measure;
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
    return staff;
}

/// Map an articulation-definition glyph (its music-font character + font id) to a
/// musical articulation. These are the stable Maestro/Engraver music-font code
/// points, so arbitrary definitions resolve, not just the document defaults. An
/// unknown glyph returns nullopt (skipped rather than guessed).
[[nodiscard]] std::optional<ir::Articulation> artic_from_glyph(std::uint16_t glyph,
                                                               std::uint16_t font) {
    if (font == 22 && (glyph == 0x55 || glyph == 0x75)) {
        return ir::Articulation::Fermata; // dedicated fermata font, 'U' / 'u'
    }
    switch (glyph) {
    case 0x2E: // '.'
        return ir::Articulation::Staccato;
    case 0x3E: // '>'
        return ir::Articulation::Accent;
    case 0x2D: // '-'
    case 0xF8: // Maestro tenuto line
        return ir::Articulation::Tenuto;
    case 0x5E: // '^'
        return ir::Articulation::StrongAccent;
    case 0x27: // wedge
        return ir::Articulation::Staccatissimo;
    default:
        return std::nullopt;
    }
}

/// One lyric syllable with its hyphenation role, split from a verse text run.
struct LyricSyllable {
    std::string text;
    ir::Lyric::Syllabic syllabic{ir::Lyric::Syllabic::Single};
};

/// Split a verse text run into syllables. A space is a word boundary; a hyphen
/// joins syllables within a word. The role (single/begin/middle/end) is derived
/// from whether the separators flanking a syllable are hyphens.
[[nodiscard]] std::vector<LyricSyllable> split_verse_run(const std::string& run) {
    std::vector<LyricSyllable> out;
    char prev_sep = ' '; // start of run behaves like a word boundary
    for (std::size_t i = 0; i < run.size();) {
        const char c = run[i];
        if (c == ' ' || c == '-') {
            prev_sep = c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < run.size() && run[j] != ' ' && run[j] != '-') {
            ++j;
        }
        const char next_sep = (j < run.size()) ? run[j] : ' '; // end behaves like a boundary
        const bool before = (prev_sep == '-');
        const bool after = (next_sep == '-');
        LyricSyllable syl;
        syl.text = run.substr(i, j - i);
        if (before && after) {
            syl.syllabic = ir::Lyric::Syllabic::Middle;
        } else if (before) {
            syl.syllabic = ir::Lyric::Syllabic::End;
        } else if (after) {
            syl.syllabic = ir::Lyric::Syllabic::Begin;
        } else {
            syl.syllabic = ir::Lyric::Syllabic::Single;
        }
        out.push_back(std::move(syl));
        i = j;
    }
    return out;
}

/// Parse the type-18 Text pool into per-verse syllable lists. The text is a run of
/// '^command(args)' blocks; a verse is '^verse(N) ... ^end' with inline format
/// commands stripped, leaving the displayable run.
[[nodiscard]] std::map<int, std::vector<LyricSyllable>> parse_verses(const std::string& text) {
    std::map<int, std::vector<LyricSyllable>> out;
    const std::string marker = "^verse(";
    std::size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        std::size_t p = pos + marker.size();
        int n = 0;
        bool any_digit = false;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
            n = n * 10 + (text[p] - '0');
            ++p;
            any_digit = true;
        }
        if (p < text.size() && text[p] == ')') {
            ++p;
        }
        const std::size_t end = text.find("^end", p);
        const std::string block =
            (end == std::string::npos) ? text.substr(p) : text.substr(p, end - p);

        // Strip inline '^command(args)' / '^command' tokens; keep the rest as run.
        std::string run;
        for (std::size_t i = 0; i < block.size();) {
            if (block[i] != '^') {
                run.push_back(block[i]);
                ++i;
                continue;
            }
            ++i; // skip '^'
            while (i < block.size()) {
                const char lc = static_cast<char>(block[i] | 0x20);
                if (lc < 'a' || lc > 'z') {
                    break;
                }
                ++i;
            }
            if (i < block.size() && block[i] == '(') {
                int depth = 0;
                while (i < block.size()) {
                    if (block[i] == '(') {
                        ++depth;
                    } else if (block[i] == ')') {
                        --depth;
                        if (depth == 0) {
                            ++i;
                            break;
                        }
                    }
                    ++i;
                }
            }
        }
        const std::size_t a = run.find_first_not_of(' ');
        const std::size_t b = run.find_last_not_of(' ');
        run = (a == std::string::npos) ? std::string{} : run.substr(a, b - a + 1);
        if (any_digit) {
            out[n] = split_verse_run(run);
        }
        pos = p;
    }
    return out;
}

/// Decode the per-entry feature attachments (slurs, articulations, lyrics) from
/// the Others and Details pools, keyed by entry id.
[[nodiscard]] EntryFeatures
decode_entry_features(const std::vector<container::OtherRecord>& others,
                      const std::vector<container::DetailRecord>& details,
                      const std::string& verse_text) {
    EntryFeatures feats;

    // Slurs are entry-attached smart shapes. A smart-shape definition (cmper) is
    // spread over a run of 'xS' Others records whose 12-byte payloads concatenate
    // into one blob; its first word is the shape type (15 = slur). The endpoints
    // are 'xE' Details records: inci = endpoint entry id, data[0] = the def cmper.
    std::map<std::uint16_t, int> shape_type;
    {
        std::map<std::uint16_t, std::vector<std::uint8_t>> blob;
        for (const auto& r : others) {
            if (r.tag == container::kTagSmartShapeSeg) {
                std::vector<std::uint8_t>& v = blob[r.cmper];
                for (const std::uint16_t w : r.data) {
                    v.push_back(static_cast<std::uint8_t>(w & 0xFFu));
                    v.push_back(static_cast<std::uint8_t>((w >> 8) & 0xFFu));
                }
            }
        }
        for (const auto& [id, v] : blob) {
            if (v.size() >= 2) {
                shape_type[id] = v[0] | (v[1] << 8);
            }
        }
    }
    std::map<std::uint16_t, std::vector<std::uint16_t>> endpoints;
    for (const auto& d : details) {
        if (d.tag == container::kDetailTagSmartShapeEntry) {
            endpoints[d.data[0]].push_back(d.inci);
        }
    }
    int slur_number = 0;
    for (const auto& [def_id, ents] : endpoints) {
        const auto st = shape_type.find(def_id);
        if (st == shape_type.end() || st->second != 15 || ents.size() < 2) {
            continue; // only true slurs (shape type 15) with two endpoints
        }
        std::uint16_t lo = ents.front();
        std::uint16_t hi = ents.front();
        for (const std::uint16_t e : ents) {
            lo = std::min(lo, e);
            hi = std::max(hi, e);
        }
        const int number = (slur_number++ % 6) + 1; // MusicXML slur numbers 1..6
        feats.slur[lo].start = number;
        feats.slur[hi].stop = number;
    }

    // Articulations: each 'MI' Details record (inci = entry id, data[0] = the
    // articulation-definition id) resolves through the 'XI' definition library in
    // the Others pool, whose first incidence per cmper carries the glyph char
    // (data[0]) and music-font id (high byte of data[1]).
    std::map<std::uint16_t, ir::Articulation> def_artic;
    {
        std::map<std::uint16_t, std::pair<std::uint16_t, std::uint16_t>> def_glyph;
        for (const auto& r : others) {
            if (r.tag == container::kTagArticDef &&
                def_glyph.find(r.cmper) == def_glyph.end()) {
                def_glyph.emplace(
                    r.cmper, std::pair<std::uint16_t, std::uint16_t>{
                                 r.data[0], static_cast<std::uint16_t>((r.data[1] >> 8) & 0xFFu)});
            }
        }
        for (const auto& [id, g] : def_glyph) {
            if (const auto a = artic_from_glyph(g.first, g.second)) {
                def_artic.emplace(id, *a);
            }
        }
    }
    for (const auto& d : details) {
        if (d.tag == container::kDetailTagArtic) {
            const auto it = def_artic.find(d.data[0]);
            if (it != def_artic.end()) {
                feats.artic[d.inci].push_back(it->second);
            }
        }
    }

    // Lyrics: each 'ev' Details record (inci = entry id, data[0] = verse number,
    // data[1] = 1-based syllable index) selects a syllable from the verse text
    // decoded out of the type-18 Text pool.
    const std::map<int, std::vector<LyricSyllable>> verses = parse_verses(verse_text);
    for (const auto& d : details) {
        if (d.tag != container::kDetailTagLyricEntry) {
            continue;
        }
        const int verse = static_cast<int>(d.data[0]);
        const int index = static_cast<int>(d.data[1]); // 1-based syllable index
        const auto vit = verses.find(verse);
        if (vit == verses.end() || index < 1 ||
            static_cast<std::size_t>(index) > vit->second.size()) {
            continue;
        }
        const LyricSyllable& syl = vit->second[static_cast<std::size_t>(index - 1)];
        ir::Lyric ly;
        ly.verse = verse;
        ly.syllabic = syl.syllabic;
        ly.text = syl.text;
        feats.lyric[d.inci].push_back(ly);
    }

    return feats;
}

/// Map a music-font (Maestro) dynamic glyph code to its dynamic string. These
/// code points are stable across the default Finale dynamics library. Returns
/// nullopt for an unrecognized glyph (the caller then tries a plain-text reading).
[[nodiscard]] std::optional<std::string> dynamic_from_glyph(unsigned glyph) {
    switch (glyph) {
    case 0x70: // 'p'
        return "p";
    case 0xB9:
        return "pp";
    case 0xB8:
        return "ppp";
    case 0x6D: // 'm'
        return "m";
    case 0x50: // 'P'
        return "mp";
    case 0x46: // 'F'
        return "mf";
    case 0x66: // 'f'
        return "f";
    case 0xC4:
        return "ff";
    case 0xEC:
        return "fff";
    case 0x53: // 'S'
        return "sf";
    case 0x5A: // 'Z'
        return "fz";
    case 0xA7:
        return "sfz";
    case 0xEA:
        return "fp";
    default:
        return std::nullopt;
    }
}

/// Decode the measure-level directions (dynamics and hairpins) from the Others
/// pool, keyed by measure number. Dynamics are 'YD' expression assignments whose
/// definition ('TD') glyph gives the dynamic text; hairpins are smart shapes of
/// type 3 (crescendo) or 4 (diminuendo), anchored to their start/end measures.
[[nodiscard]] std::map<std::uint16_t, std::vector<ir::Direction>>
decode_measure_directions(const std::vector<container::OtherRecord>& others) {
    std::map<std::uint16_t, std::vector<ir::Direction>> dirs;

    // Text-expression definitions: cmper -> dynamic text. A def is two consecutive
    // 'TD' records; the first marks a music-font (dynamic) def with data[0]==0x1800
    // and the second carries the displayed text (ASCII for the common dynamics).
    std::map<std::uint16_t, std::string> def_dynamic;
    {
        std::map<std::uint16_t, std::vector<const container::OtherRecord*>> td;
        for (const auto& r : others) {
            if (r.tag == container::kTagTextExprDef) {
                td[r.cmper].push_back(&r);
            }
        }
        for (const auto& [id, recs] : td) {
            if (recs.size() < 2 || recs[0]->data[0] != 0x1800u) {
                continue; // only music-font (dynamic) definitions
            }
            // The library dynamics are single music-font glyphs; map the glyph to
            // its dynamic string.
            const unsigned glyph = recs[1]->data[0] & 0xFFu;
            if (const auto mapped = dynamic_from_glyph(glyph)) {
                def_dynamic[id] = *mapped;
                continue;
            }
            // Fallback: a dynamic typed as plain ASCII text rather than a glyph.
            std::string s;
            for (const std::uint16_t w : recs[1]->data) {
                const char c0 = static_cast<char>(w & 0xFFu);
                if (c0 == '\0') {
                    break;
                }
                s.push_back(c0);
                const char c1 = static_cast<char>((w >> 8) & 0xFFu);
                if (c1 == '\0') {
                    break;
                }
                s.push_back(c1);
            }
            bool printable = !s.empty();
            for (const char c : s) {
                if (c < 0x20 || c > 0x7E) {
                    printable = false;
                    break;
                }
            }
            if (printable) {
                def_dynamic[id] = s;
            }
        }
    }

    // 'YD' assignments: cmper = measure, data[0] = def id, data[1] = signed
    // horizontal EDU position within the measure.
    for (const auto& r : others) {
        if (r.tag != container::kTagExprAssign) {
            continue;
        }
        const auto it = def_dynamic.find(r.data[0]);
        if (it == def_dynamic.end()) {
            continue;
        }
        ir::Direction d;
        d.kind = ir::Direction::Kind::Dynamic;
        d.dynamic = it->second;
        const auto edu = static_cast<std::int16_t>(r.data[1]);
        d.position = edu > 0 ? static_cast<ir::Edu>(edu) : 0;
        // data word 5 = staff assignment (0x10NN); low byte is the staff number,
        // 0 means a single-staff piece (default to the first part).
        d.staff = (r.data[5] != 0) ? static_cast<std::uint16_t>(r.data[5] & 0xFFu) : 0;
        dirs[r.cmper].push_back(std::move(d));
    }

    // Hairpins: smart-shape definitions whose concatenated 'xS' blob has type 3
    // (crescendo) or 4 (diminuendo). The left endpoint measure is at blob+6, the
    // right endpoint measure at blob+0x24.
    {
        std::map<std::uint16_t, std::vector<std::uint8_t>> blob;
        for (const auto& r : others) {
            if (r.tag == container::kTagSmartShapeSeg) {
                std::vector<std::uint8_t>& v = blob[r.cmper];
                for (const std::uint16_t w : r.data) {
                    v.push_back(static_cast<std::uint8_t>(w & 0xFFu));
                    v.push_back(static_cast<std::uint8_t>((w >> 8) & 0xFFu));
                }
            }
        }
        for (const auto& [id, v] : blob) {
            (void)id;
            if (v.size() < 0x26) {
                continue;
            }
            const int type = v[0] | (v[1] << 8);
            if (type != 3 && type != 4) {
                continue; // not a hairpin
            }
            const auto start_meas = static_cast<std::uint16_t>(v[6] | (v[7] << 8));
            const auto end_meas = static_cast<std::uint16_t>(v[0x24] | (v[0x25] << 8));
            ir::Direction s;
            s.kind = ir::Direction::Kind::WedgeStart;
            s.crescendo = (type == 3);
            s.position = 0;
            dirs[start_meas].push_back(std::move(s));
            ir::Direction e;
            e.kind = ir::Direction::Kind::WedgeStop;
            e.position = 1 << 20; // emit after the measure's notes
            dirs[end_meas].push_back(std::move(e));
        }
    }

    return dirs;
}

/// Pick a clef for a voice from its average notated pitch: low voices read better
/// in bass clef. A fallback for late-era (zlib) files until the real per-staff
/// clef from the type-26 staff spec is decoded.
[[nodiscard]] ir::Clef clef_for_chain(const std::vector<container::EntryRecord>& chain) {
    long long sum = 0;
    long long n = 0;
    for (const auto& e : chain) {
        for (const auto& note : e.notes) {
            sum += note.step_from_c4;
            ++n;
        }
    }
    if (n > 0 && static_cast<double>(sum) / static_cast<double>(n) < -2.0) {
        return ir::Clef{ir::ClefSign::F, 4}; // bass
    }
    return ir::Clef{ir::ClefSign::G, 2}; // treble
}

// ----------------------------------------------------------------------------
// Document metadata (work title) from the Enigma text pool.
// ----------------------------------------------------------------------------

/// Reduce one Enigma text block to its display text: the longest run of printable
/// ASCII once command framing and insert glyphs are removed. Two command forms
/// appear in the text pool - readable "^name(args)" (staff/group names) and binary
/// "^<0x80+><control-bytes>" (page texts such as the title). Both are skipped, as
/// are stray high-byte inserts (e.g. the page-number glyph that trails a title).
[[nodiscard]] std::string clean_text_block(std::string_view seg) {
    std::string best;
    std::string cur;
    const auto trim = [](const std::string& s) {
        std::size_t a = 0;
        std::size_t b = s.size();
        while (a < b && s[a] == ' ') {
            ++a;
        }
        while (b > a && s[b - 1] == ' ') {
            --b;
        }
        return s.substr(a, b - a);
    };
    const auto flush = [&]() {
        const std::string t = trim(cur);
        if (t.size() > best.size()) {
            best = t;
        }
        cur.clear();
    };
    const auto is_alpha = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    std::size_t i = 0;
    while (i < seg.size()) {
        const auto c = static_cast<unsigned char>(seg[i]);
        if (c == '^') {
            flush();
            if (i + 1 >= seg.size()) {
                ++i;
                continue;
            }
            if (is_alpha(static_cast<unsigned char>(seg[i + 1]))) {
                // Readable command: skip the name, then an optional "(...)".
                ++i;
                while (i < seg.size() && is_alpha(static_cast<unsigned char>(seg[i]))) {
                    ++i;
                }
                if (i < seg.size() && seg[i] == '(') {
                    while (i < seg.size() && seg[i] != ')') {
                        ++i;
                    }
                    if (i < seg.size()) {
                        ++i; // past ')'
                    }
                }
            } else {
                // Binary command: skip '^', the command byte, and its control args.
                i += 2;
                while (i < seg.size() && static_cast<unsigned char>(seg[i]) < 0x20) {
                    ++i;
                }
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            cur.push_back(static_cast<char>(c));
        } else {
            flush(); // a stray insert / control byte ends the current run
        }
        ++i;
    }
    flush();
    return best;
}

/// Pull the work title from the Enigma text pool. Text is framed as
/// "^block(id)...^end"; instrument and group names use the readable "^font(...)"
/// form while page texts (title, composer, source) use the binary command form.
/// The title is reliably the first binary-framed page-text block with alphabetic
/// content, so that is what we return. Composer/subtitle disambiguation needs the
/// page-text position records and is deferred. Returns "" when nothing qualifies.
[[nodiscard]] std::string extract_work_title(const std::string& pool,
                                             const std::vector<std::string>& staff_names) {
    const std::string head = "^block(";
    std::size_t pos = 0;
    while ((pos = pool.find(head, pos)) != std::string::npos) {
        const std::size_t id_start = pos + head.size();
        const std::size_t close = pool.find(')', id_start);
        if (close == std::string::npos) {
            break;
        }
        const std::size_t body_start = close + 1;
        const std::size_t end = pool.find("^end", body_start);
        const std::size_t body_end = (end == std::string::npos) ? pool.size() : end;
        const std::string_view seg(pool.data() + body_start, body_end - body_start);
        pos = body_end;

        // Page texts open with a binary command ('^' then a byte >= 0x80) right
        // after "^block(id)". Readable "^font(...)" blocks are instrument/group
        // names, never the title.
        if (seg.size() < 2 || static_cast<unsigned char>(seg[0]) != '^' ||
            static_cast<unsigned char>(seg[1]) < 0x80) {
            continue;
        }
        const std::string text = clean_text_block(seg);
        if (text.size() < 3) {
            continue;
        }
        bool has_alpha = false;
        for (const char ch : text) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                has_alpha = true;
                break;
            }
        }
        if (!has_alpha) {
            continue;
        }
        bool is_staff_name = false;
        for (const std::string& nm : staff_names) {
            if (!nm.empty() && nm == text) {
                is_staff_name = true;
                break;
            }
        }
        if (is_staff_name) {
            continue;
        }
        return text;
    }
    return std::string{};
}

/// Build a score when there are no 2003-era measure specs / frame holders, e.g. a
/// Finale 2010+ (zlib) file whose Others/Details pools use the new framing we do
/// not decode yet. Each entry-chain head (an entry that nothing links to) becomes
/// one part; the chain is split into 4/4 measures (the common case), padded with
/// rests, under default C-major / treble attributes. The notes survive even
/// before the late-era measure/staff specs are decoded.
[[nodiscard]] Result<ir::Score>
build_score_from_chains(const std::vector<container::EntryRecord>& entries,
                        const container::Doc2011& doc, Diagnostics& diags) {
    using namespace ir;
    std::map<std::uint16_t, const container::EntryRecord*> by_id;
    std::set<std::uint16_t> linked;
    for (const auto& e : entries) {
        by_id.emplace(e.id, &e);
        if (e.next_id != 0) {
            linked.insert(e.next_id);
        }
    }
    std::vector<std::uint16_t> heads;
    for (const auto& e : entries) {
        if (linked.find(e.id) == linked.end()) {
            heads.push_back(e.id);
        }
    }
    std::sort(heads.begin(), heads.end());
    if (heads.empty()) {
        return Result<Score>::fail(ErrorCode::NotImplemented,
                                   "no entry-chain heads found; cannot build a score");
    }

    const auto chain_from = [&by_id](std::uint16_t first) {
        std::vector<container::EntryRecord> chain;
        std::uint16_t cur = first;
        int guard = 0;
        while (cur != 0 && guard++ < 65536) {
            const auto it = by_id.find(cur);
            if (it == by_id.end()) {
                break;
            }
            chain.push_back(*it->second);
            cur = it->second->next_id;
        }
        return chain;
    };

    // One key + time for the whole piece (Finale 2010+ stores them once per active
    // staff, not per measure); default to 4/4 C major when the Others pool is absent.
    const TimeSignature time =
        doc.found ? derive_time_signature(doc.beats, doc.divbeat) : TimeSignature{4, 4};
    const KeySignature key =
        doc.found ? key_from_field(doc.key_field) : KeySignature{0, KeySignature::Mode::Major};
    const Edu bar = std::max<Edu>(measure_capacity(time), 1);

    // Follow each head's chain; size every part to the longest voice.
    std::vector<std::vector<container::EntryRecord>> chains;
    std::size_t num_measures = 1;
    for (const std::uint16_t head : heads) {
        std::vector<container::EntryRecord> chain = chain_from(head);
        Edu total = 0;
        for (const auto& e : chain) {
            total += e.duration_edu;
        }
        const auto bars = static_cast<std::size_t>((total + bar - 1) / bar);
        num_measures = std::max(num_measures, std::max<std::size_t>(bars, 1));
        chains.push_back(std::move(chain));
    }

    // Name the voices: a constant-pitch chain is the percussion click-track; the
    // melodic chains take the decoded staff names, highest pitch first.
    std::vector<std::string> part_names(chains.size());
    {
        std::vector<std::pair<double, std::size_t>> melodic; // (average pitch, chain index)
        for (std::size_t i = 0; i < chains.size(); ++i) {
            long long sum = 0;
            long long n = 0;
            std::set<int> steps;
            for (const auto& e : chains[i]) {
                for (const auto& note : e.notes) {
                    sum += note.step_from_c4;
                    ++n;
                    steps.insert(note.step_from_c4);
                }
            }
            if (n >= 4 && steps.size() == 1) {
                part_names[i] = "Percussion"; // constant-pitch click-track
            } else if (n > 0) {
                melodic.emplace_back(static_cast<double>(sum) / static_cast<double>(n), i);
            }
        }
        std::sort(melodic.begin(), melodic.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t k = 0; k < melodic.size() && k < doc.staff_names.size(); ++k) {
            part_names[melodic[k].second] = doc.staff_names[k];
        }
    }

    const EntryFeatures feats; // late-era slur/artic/lyric details not decoded yet

    Score score;
    for (std::size_t i = 0; i < chains.size(); ++i) {
        const Clef clef = clef_for_chain(chains[i]);
        Staff staff;
        staff.initial_clef = clef;
        for (std::size_t m = 0; m < num_measures; ++m) {
            Measure measure;
            if (m == 0) {
                measure.key = key;
                measure.time = time;
                measure.clef = clef;
            }
            staff.measures.push_back(std::move(measure));
        }
        place_voice(staff, chains[i], feats, diags);
        Part part;
        part.id = "P" + std::to_string(i + 1);
        part.name = part_names[i].empty() ? ("Part " + std::to_string(i + 1)) : part_names[i];
        part.staves.push_back(std::move(staff));
        score.parts.push_back(std::move(part));
    }
    diags.info("built a score from " + std::to_string(heads.size()) +
               " entry-chain heads (no measure specs; defaulted to 4/4 C major)");
    return Result<Score>::ok(std::move(score));
}

/// Layer 2: build an ir::Score from the decoded Others, note entries, and frame
/// holders. Each staff becomes one Part: its clef comes from its staff spec, and
/// its notes are the entry chain reachable from its frame holder, then placed
/// measure-by-measure. Returns an Error if there are no measure specs.
[[nodiscard]] Result<ir::Score>
build_score(const std::vector<container::OtherRecord>& others,
            const std::vector<container::EntryRecord>& entries,
            const std::vector<container::FrameHolder>& holders,
            const std::vector<container::DetailRecord>& details, const std::string& verse_text,
            const container::Doc2011& doc2011, Diagnostics& diags) {
    using namespace ir;

    const EntryFeatures feats = decode_entry_features(others, details, verse_text);

    // Measures: first Measure-Spec incidence per measure number (ascending).
    std::map<std::uint16_t, const container::OtherRecord*> measure_specs;
    for (const auto& record : others) {
        if (record.tag == container::kTagMeasureSpec) {
            measure_specs.emplace(record.cmper, &record);
        }
    }
    if (measure_specs.empty()) {
        // No 2003-era measure specs. If note entries are present (e.g. a Finale
        // 2010+ zlib file whose Others pool uses the new framing), fall back to
        // building a score from the entry-chain heads so the notes still convert.
        if (!entries.empty()) {
            return build_score_from_chains(entries, doc2011, diags);
        }
        return Result<Score>::fail(ErrorCode::NotImplemented,
                                   "no measure-spec records found; cannot build a score");
    }
    std::vector<std::uint16_t> measure_cmpers;
    measure_cmpers.reserve(measure_specs.size());
    for (const auto& kv : measure_specs) {
        measure_cmpers.push_back(kv.first);
    }
    const std::map<std::uint16_t, std::vector<ir::Direction>> measure_dirs =
        decode_measure_directions(others);

    // Staves: each distinct Staff-Spec cmper is a staff; its clef is data[0] of
    // the 2nd incidence (treble = 0, bass = 3).
    std::map<std::uint16_t, int> staff_clef;
    {
        std::map<std::uint16_t, int> incidence;
        for (const auto& record : others) {
            if (record.tag == container::kTagStaffSpec && ++incidence[record.cmper] == 2) {
                staff_clef[record.cmper] = static_cast<int>(record.data[0]);
            }
        }
    }
    if (staff_clef.empty()) {
        staff_clef.emplace(static_cast<std::uint16_t>(1), 0); // fallback: one treble staff
    }

    // Entries by id, and each staff's chain start (its lowest-measure holder).
    std::map<std::uint16_t, const container::EntryRecord*> by_id;
    for (const auto& e : entries) {
        by_id.emplace(e.id, &e);
    }
    // Per staff, the lowest-measure holder gives its clef and each layer's chain
    // start; a layer becomes a MusicXML voice.
    std::map<std::uint16_t, std::array<std::uint16_t, 3>> staff_voices;
    std::map<std::uint16_t, std::uint16_t> staff_start_meas;
    std::map<std::uint16_t, int> staff_fg_clef;
    for (const auto& h : holders) {
        if (h.voice_first[0] == 0 && h.voice_first[1] == 0 && h.voice_first[2] == 0) {
            continue;
        }
        const auto m = staff_start_meas.find(h.staff);
        if (m == staff_start_meas.end() || h.measure < m->second) {
            staff_start_meas[h.staff] = h.measure;
            staff_voices[h.staff] = h.voice_first;
            staff_fg_clef[h.staff] = static_cast<int>(h.clef);
        }
    }

    // Collect a next-link chain of entries starting at `first`.
    const auto chain_from = [&by_id](std::uint16_t first) {
        std::vector<container::EntryRecord> chain;
        std::uint16_t cur = first;
        int guard = 0;
        while (cur != 0 && guard++ < 8192) {
            const auto it = by_id.find(cur);
            if (it == by_id.end()) {
                break;
            }
            chain.push_back(*it->second);
            cur = it->second->next_id;
        }
        return chain;
    };

    const bool multi = staff_clef.size() > 1u;
    Score score;
    for (const auto& [staff_no, is_clef] : staff_clef) {
        // Prefer the clef from the staff's frame holder (it can differ per staff,
        // e.g. bass for a piano's lower staff); fall back to the staff spec.
        const auto fg = staff_fg_clef.find(staff_no);
        const int clef_idx = (fg != staff_fg_clef.end()) ? fg->second : is_clef;
        const Clef clef = clef_from_index(clef_idx);
        Staff staff = build_measure_skeleton(measure_specs, clef);

        // Each non-empty layer in the staff's holder becomes a voice.
        int voices_placed = 0;
        const auto vit = staff_voices.find(staff_no);
        if (vit != staff_voices.end()) {
            for (const std::uint16_t first : vit->second) {
                if (first == 0 || by_id.find(first) == by_id.end()) {
                    continue;
                }
                place_voice(staff, chain_from(first), feats, diags);
                ++voices_placed;
            }
        }
        if (voices_placed == 0) {
            place_voice(staff, {}, feats, diags); // empty staff: a single voice of rests
        }

        // Attach this staff's measure-level directions. A staffed dynamic goes to
        // its matching staff; a default-staff direction (staff 0, e.g. a hairpin)
        // goes to the first part only, so it is not duplicated across staves.
        const bool first_part = score.parts.empty();
        for (std::size_t i = 0; i < staff.measures.size() && i < measure_cmpers.size(); ++i) {
            const auto dit = measure_dirs.find(measure_cmpers[i]);
            if (dit == measure_dirs.end()) {
                continue;
            }
            for (const ir::Direction& d : dit->second) {
                if (d.staff == staff_no || (d.staff == 0 && first_part)) {
                    staff.measures[i].directions.push_back(d);
                }
            }
        }

        Part part;
        part.id = "P" + std::to_string(staff_no);
        part.name = multi ? ("Staff " + std::to_string(staff_no)) : "Music";
        part.staves.push_back(std::move(staff));
        score.parts.push_back(std::move(part));
    }
    return Result<Score>::ok(std::move(score));
}

} // namespace

ir::Score build_trivial_score() {
    using namespace ir;

    // The measure: a single quarter-note middle C (C4) followed by rests that
    // fill out the 4/4 bar (half + quarter = 3072 EDU) so the measure sums to a
    // full 4096 EDU. This mirrors the trailing-rest fill Finale emits.
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

    // Layer 1.5: decode the compressed "Others" pool (measure/staff specs).
    Result<std::vector<container::OtherRecord>> others = container::read_others_pool(data, diags);
    if (!others) {
        return Result<std::string>::fail(others.code(), others.message());
    }

    // Layer 1.5: decode the plain note "entry" pool and the frame holders.
    Result<std::vector<container::EntryRecord>> entries = container::read_entry_pool(data, diags);
    const std::vector<container::EntryRecord> entry_list =
        entries ? entries.value() : std::vector<container::EntryRecord>{};
    if (!entries) {
        diags.warn("entry pool unreadable (" + entries.message() + "); measures left as rests");
    }
    Result<std::vector<container::FrameHolder>> holders = container::read_frame_holders(data, diags);
    const std::vector<container::FrameHolder> holder_list =
        holders ? holders.value() : std::vector<container::FrameHolder>{};
    Result<std::vector<container::DetailRecord>> details = container::read_details_pool(data, diags);
    const std::vector<container::DetailRecord> detail_list =
        details ? details.value() : std::vector<container::DetailRecord>{};
    Result<std::string> text = container::read_text_pool(data, diags);
    const std::string verse_text = text ? text.value() : std::string{};
    Result<container::Doc2011> doc = container::read_doc_2011(data, diags);
    const container::Doc2011 doc2011 = doc ? doc.value() : container::Doc2011{};

    // Layer 2: build the IR (one part per staff, with notes).
    Result<ir::Score> score = build_score(others.value(), entry_list, holder_list, detail_list,
                                          verse_text, doc2011, diags);
    if (!score) {
        return Result<std::string>::fail(score.code(), score.message());
    }

    // Document metadata: fill the work title from the text pool when the score
    // builder did not already set one.
    if (score.value().work_title.empty()) {
        std::string title = extract_work_title(verse_text, doc2011.staff_names);
        if (!title.empty()) {
            diags.info("extracted work title: " + title);
            score.value().work_title = std::move(title);
        }
    }

    // Layer 3: emit MusicXML.
    return musicxml::emit_score_partwise(score.value());
}

} // namespace rescore
