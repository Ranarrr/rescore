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

Rescore reads the common-notation content of a legacy `.mus` file and converts it to MusicXML 4.0.

**Supported today:**

- Detects the `.mus` container and era from its embedded signatures, and decompresses the document's internal data structures.
- **Notes:** pitch (name, octave, accidental), duration (whole through 16th, dotted), chords, and rests.
- **Measure attributes:** key signature, time signature (including compound meters such as 6/8 and 9/8), and clef (treble, bass, alto, tenor).
- **Structure:** multiple measures with correct barring, multiple staves and parts (such as a piano grand staff), and multiple voices per staff.
- **Marks:** ties, tuplets, slurs, articulations (staccato, accent, tenuto, marcato, fermata), lyrics with hyphenation, and dynamics (p, f, mp, mf, and so on, plus crescendo and diminuendo hairpins).
- Emits `score-partwise` **MusicXML 4.0**.
- An optional desktop **GUI**: drag a `.mus` file in and get a `.musicxml` next to it.

**Not yet handled:** grace notes, ornaments, beaming detail, chord symbols, repeats and endings, page layout / fonts / engraving, and MIDI/PDF/SVG export. Semantic fidelity comes first: the goal is that a musician opens the result and recognizes their piece.

This is a digital-preservation project; please contribute sample files and bug reports.

---

## Build

Rescore is C++20 and builds with CMake (>= 3.24) on Windows, macOS, and Linux. The converter and CLI have no third-party dependencies; the test suite and the optional GUI fetch theirs automatically via CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

By default this builds the **`rescore-cli`** command-line tool, written to `build/bin/` (under a per-config subfolder, e.g. `build/bin/Release/`, on multi-config generators such as Visual Studio).

The desktop **GUI** (the main `rescore` app) is built with `-DRESCORE_BUILD_GUI=ON`. On Linux it needs X11/OpenGL/GTK development packages (e.g. `xorg-dev libgl1-mesa-dev libgtk-3-dev`). For a dependency-free build of just the CLI, add `-DRESCORE_BUILD_TESTS=OFF`.

---

## Use

**GUI:** open the `rescore` app and drag a `.mus` file onto the window (or pick one with the file dialog). It writes a `.musicxml` next to the original.

**CLI:** convert a single file:

```sh
rescore-cli in.mus -o out.musicxml
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
