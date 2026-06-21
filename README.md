# Rescore

**Finale is gone. Your music isn't.**

Rescore opens legacy pre-2014 Finale `.mus` files and exports clean MusicXML 4.0. **No Finale required, no upload, your scores never leave your computer.**

Free and open-source (MIT).

---

## Why

MakeMusic discontinued Finale in 2024 after ~35 years. The universal advice, "just open it in Finale and export MusicXML," stops working the moment Finale will no longer authorize. Composers, teachers, libraries, and archives are left with decades of `.mus` files and no path forward.

Other tools need Finale installed, or only handle the newer `.musx` files. **Rescore reads the legacy pre-2014 `.mus` format directly** and converts it to MusicXML 4.0, the open interchange format that MuseScore, Dorico, and Sibelius all import. Like reading an old `.doc`, this is format conversion: legacy `.mus` has no encryption and no DRM, only an undocumented layout.

This is a digital-preservation project first. Expect rough edges; please contribute sample files and bug reports.

---

## Status

Rescore is early and under active development.

- **Works today:**
  - Detects the `.mus` container and era from its embedded signatures.
  - Reads and decompresses the document's internal data structures.
  - Extracts **key signature, time signature, and clef**, including compound meters (6/8, 9/8) and treble/bass clefs.
  - Emits `score-partwise` **MusicXML 4.0**.
- **In progress:** per-note data (pitch, duration, and accidentals) so that converted measures carry their notes, not just their attributes. This is the next milestone; until it lands, output carries each measure's key/time/clef.

---

## What the first release targets

- One part, one staff, one voice.
- Key signature, time signature, clef (treble and bass at minimum).
- Pitch: note name + octave + accidental (sharp / flat / natural).
- Rhythm: whole through 16th notes and rests, including dotted.
- Chords (multiple notes in one entry) and basic ties.
- Multiple measures in sequence, with correct barring.
- Output: `score-partwise` MusicXML 4.0.

Deliberately out of scope for the first release: multiple staves/parts, multiple voices, tuplets, grace notes, beaming detail, slurs/articulations/dynamics/ornaments, lyrics, chord symbols, repeats and endings, all engraving/layout/fonts, MIDI/PDF/SVG export, and a GUI. Semantic fidelity comes first: the goal is that a musician recognizes their piece.

---

## Build

Rescore is C++20 and builds with CMake (>= 3.24) on Windows, macOS, and Linux, with no third-party dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The `rescore` executable is written to `build/bin/` (under a per-config subfolder, e.g. `build/bin/Release/`, for multi-config generators such as Visual Studio).

---

## Use

Convert a single file:

```sh
rescore in.mus -o out.musicxml
```

Useful flags:

- `--dump` prints a summary of the detected container (era, version string, byte order)
- `--help` prints the full usage

Output is `score-partwise` MusicXML 4.0.

---

## Privacy

**Your scores never leave your computer. Nothing is uploaded.** Rescore processes files entirely locally. It does not store, upload, log, retain, or transmit your files or their musical content.

---

## License

MIT. See [LICENSE](LICENSE).

---

**Not affiliated with MakeMusic; Finale is a trademark of its owner.** Product names are used only nominatively, to describe the file format Rescore reads.
