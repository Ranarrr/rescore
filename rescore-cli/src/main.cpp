// Rescore command-line interface.
//
// Rescore converts LEGACY Finale `.mus` files (the undocumented "Enigma Binary
// File" format, earliest Finale through Finale 2012) into MusicXML 4.0. No
// Finale installation is required.
//
// This translation unit owns ONLY the user-facing shell: argument parsing,
// bounded and error-checked file I/O, and process exit codes. Every notation
// concern lives behind the rescore::core public API. The CLI never throws and
// never crashes on malformed, empty, or oversized input: all failures are
// surfaced as friendly diagnostics and non-zero exit codes.
//
// Public core API consumed (Layer 1/2/3 contract):
//   rescore::detect_version(bytes)                   -> EnigmaVersion (era + label)
//   rescore::container::ContainerReader::parse(...)  -> Result<RawDocument>
//   rescore::container::dump(doc, source, os)        -> --dump rendering
//   rescore::convert_mus_to_musicxml(bytes, diags)   -> Result<std::string>
//   rescore::build_trivial_score()                   -> ir::Score (gold fixture)
//   rescore::musicxml::emit_score_partwise(score)    -> Result<std::string>
//
// Not affiliated with MakeMusic; Finale is a trademark of its owner.

#include "rescore/container.hpp"
#include "rescore/convert.hpp"
#include "rescore/ir.hpp"
#include "rescore/musicxml.hpp"
#include "rescore/result.hpp"
#include "rescore/version.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Process exit codes. Kept small and stable so batch scripts can branch on them.
enum class ExitCode : int {
    Ok = 0,            // success
    Usage = 2,         // bad/contradictory command-line arguments
    IoError = 3,       // file could not be read or written
    NotImplemented = 4 // legacy .mus parsing not yet available for this input
};

[[nodiscard]] constexpr int to_int(ExitCode code) noexcept {
    return static_cast<int>(code);
}

// Upper bound on the size of a file we are willing to slurp into memory. Legacy
// `.mus` documents are small (kilobytes to low single-digit megabytes); this
// guard keeps a hostile or corrupt "huge" file from exhausting memory before we
// have even looked at it. 256 MiB is comfortably above any real Finale score.
constexpr std::uintmax_t kMaxInputBytes = 256ull * 1024ull * 1024ull;

// ---------------------------------------------------------------------------
// Small output helpers. We write user-facing prose to stderr and machine
// output (MusicXML on stdout when no -o is given, dumps, etc.) to stdout.
// ---------------------------------------------------------------------------

void print_disclaimer(std::ostream &os) {
    os << "Not affiliated with MakeMusic; Finale is a trademark of its owner.\n";
}

// Version string is injected by the build (target_compile_definitions) from the
// CMake PROJECT_VERSION; the fallback only applies to ad-hoc compiles.
#ifndef RESCORE_CLI_VERSION
#define RESCORE_CLI_VERSION "0.0.0-dev"
#endif

void print_version(std::ostream &os) {
    os << "rescore " RESCORE_CLI_VERSION " (Rescore legacy Finale .mus -> MusicXML 4.0)\n";
    print_disclaimer(os);
}

void print_usage(std::ostream &os) {
    os << "Rescore - convert legacy Finale .mus files to MusicXML 4.0.\n"
          "Reads the undocumented \"Enigma Binary File\" format (earliest Finale\n"
          "through Finale 2012). No Finale installation required. Your scores are\n"
          "processed locally and never stored, uploaded, or retained.\n"
          "\n"
          "USAGE:\n"
          "  rescore <in.mus> -o <out.musicxml>   Convert one file to MusicXML.\n"
          "                                       If <out> ends in .mxl, the\n"
          "                                       compressed container is a\n"
          "                                       documented TODO for now.\n"
          "  rescore <in.mus> --dump              Print the detected container\n"
          "                                       version and a record dump.\n"
          "  rescore --batch <folder> -o <outdir> Convert every .mus file found\n"
          "                                       directly in <folder>.\n"
          "  rescore --emit-trivial-gold -o <file>\n"
          "                                       DEV: emit the built-in trivial\n"
          "                                       gold score through the emitter\n"
          "                                       (proves the end-to-end path).\n"
          "\n"
          "OPTIONS:\n"
          "  -o, --output <path>   Output file (single convert) or directory\n"
          "                        (--batch). If omitted on a single convert,\n"
          "                        MusicXML is written to stdout.\n"
          "  --dump                Print detected version + ContainerReader dump.\n"
          "  --keep                Keep intermediate artifacts during conversion.\n"
          "  --batch <folder>      Iterate over .mus files in <folder>.\n"
          "  --emit-trivial-gold   Emit the canonical trivial gold score (DEV).\n"
          "  --version             Print version information and exit.\n"
          "  -h, --help            Print this help and exit.\n"
          "\n";
    print_disclaimer(os);
}

// ---------------------------------------------------------------------------
// Bounded, error-checked binary file read. Returns std::nullopt on any error
// (including over-size). Never throws.
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<std::vector<std::byte>>
read_file_bytes(const fs::path &path, std::string &error_out) {
    std::error_code ec;

    if (!fs::exists(path, ec) || ec) {
        error_out = "no such file: " + path.string();
        return std::nullopt;
    }
    if (fs::is_directory(path, ec) || ec) {
        error_out = "path is a directory, expected a file: " + path.string();
        return std::nullopt;
    }

    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error_out = "could not determine size of: " + path.string();
        return std::nullopt;
    }
    if (size > kMaxInputBytes) {
        error_out = "file is too large to process (" + std::to_string(size) +
                    " bytes; limit is " + std::to_string(kMaxInputBytes) + ")";
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error_out = "could not open for reading: " + path.string();
        return std::nullopt;
    }

    std::vector<std::byte> bytes;
    bytes.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(size));
        if (in.bad()) {
            error_out = "read error on: " + path.string();
            return std::nullopt;
        }
        // Guard against a short read (e.g. file truncated mid-read).
        const std::streamsize got = in.gcount();
        if (got < 0 || static_cast<std::uintmax_t>(got) != size) {
            bytes.resize(static_cast<std::size_t>(got < 0 ? 0 : got));
        }
    }
    // Note: an empty file is a valid read; downstream detection will report it
    // as an unrecognized container rather than an I/O failure.
    return bytes;
}

// ---------------------------------------------------------------------------
// Bounded, error-checked text write (used for MusicXML output).
// ---------------------------------------------------------------------------

[[nodiscard]] bool write_text_file(const fs::path &path, std::string_view text,
                                   std::string &error_out) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        // Ignore ec here: if the directory truly cannot be created the open
        // below will fail and we report that instead.
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error_out = "could not open for writing: " + path.string();
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        error_out = "write error on: " + path.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool has_extension_ci(const fs::path &path, std::string_view ext) {
    std::string have = path.extension().string();
    for (char &c : have) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return have == ext;
}

// ---------------------------------------------------------------------------
// Parsed command line.
// ---------------------------------------------------------------------------

enum class Mode { Convert, Dump, Batch, EmitTrivialGold, Version, Help };

struct Options {
    Mode mode = Mode::Convert;
    std::optional<fs::path> input;       // single input .mus
    std::optional<fs::path> batch_dir;   // --batch folder
    std::optional<fs::path> output;      // -o / --output (file or dir)
    bool dump = false;                   // --dump requested
    bool keep = false;                   // --keep requested
};

struct ParseResult {
    std::optional<Options> options; // populated on success
    std::string error;              // populated on failure
    bool wants_help = false;        // print usage even on error
};

[[nodiscard]] ParseResult parse_args(int argc, char **argv) {
    ParseResult result;
    Options opts;

    bool saw_dump_flag = false;
    bool saw_batch_flag = false;
    bool saw_trivial_flag = false;
    std::optional<fs::path> positional_input;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        auto take_value = [&](std::string_view name,
                              std::optional<fs::path> &slot) -> bool {
            if (i + 1 >= argc) {
                result.error = std::string("option ") + std::string(name) +
                               " requires a value";
                return false;
            }
            if (slot.has_value()) {
                result.error = std::string("option ") + std::string(name) +
                               " given more than once";
                return false;
            }
            slot = fs::path(argv[++i]);
            return true;
        };

        if (arg == "-h" || arg == "--help") {
            opts.mode = Mode::Help;
            result.options = opts;
            return result;
        }
        if (arg == "--version") {
            opts.mode = Mode::Version;
            result.options = opts;
            return result;
        }
        if (arg == "--dump") {
            saw_dump_flag = true;
            opts.dump = true;
            continue;
        }
        if (arg == "--keep") {
            opts.keep = true;
            continue;
        }
        if (arg == "--emit-trivial-gold") {
            saw_trivial_flag = true;
            continue;
        }
        if (arg == "-o" || arg == "--output") {
            if (!take_value("-o", opts.output)) {
                result.wants_help = true;
                return result;
            }
            continue;
        }
        if (arg == "--batch") {
            saw_batch_flag = true;
            if (!take_value("--batch", opts.batch_dir)) {
                result.wants_help = true;
                return result;
            }
            continue;
        }
        if (!arg.empty() && arg.front() == '-' && arg != "-") {
            result.error = std::string("unknown option: ") + std::string(arg);
            result.wants_help = true;
            return result;
        }

        // Positional argument: the input .mus file.
        if (positional_input.has_value()) {
            result.error =
                "more than one input file given; only one is supported "
                "(use --batch for a folder)";
            result.wants_help = true;
            return result;
        }
        positional_input = fs::path(arg);
    }

    // Resolve the mode from the flags we saw, checking for contradictions.
    if (saw_trivial_flag) {
        if (saw_batch_flag || positional_input.has_value()) {
            result.error =
                "--emit-trivial-gold takes no input file and is not compatible "
                "with --batch";
            result.wants_help = true;
            return result;
        }
        if (!opts.output.has_value()) {
            result.error = "--emit-trivial-gold requires -o <file>";
            result.wants_help = true;
            return result;
        }
        opts.mode = Mode::EmitTrivialGold;
        result.options = opts;
        return result;
    }

    if (saw_batch_flag) {
        if (positional_input.has_value()) {
            result.error =
                "--batch and a positional input file are mutually exclusive";
            result.wants_help = true;
            return result;
        }
        if (!opts.output.has_value()) {
            result.error = "--batch requires -o <outdir>";
            result.wants_help = true;
            return result;
        }
        opts.mode = Mode::Batch;
        result.options = opts;
        return result;
    }

    // From here we need a positional input file.
    if (!positional_input.has_value()) {
        result.error = "no input file given";
        result.wants_help = true;
        return result;
    }
    opts.input = positional_input;

    // --dump without -o is a pure dump; --dump WITH -o still converts but also
    // prints the dump. With no -o and no --dump we convert to stdout.
    if (saw_dump_flag && !opts.output.has_value()) {
        opts.mode = Mode::Dump;
    } else {
        opts.mode = Mode::Convert;
    }

    result.options = opts;
    return result;
}

// ---------------------------------------------------------------------------
// Mode implementations.
// ---------------------------------------------------------------------------

// Print the detected container version plus the ContainerReader record dump for
// a single in-memory file. Returns true on success. Never throws; an
// unrecognized container is reported, not fatal beyond the exit code.
[[nodiscard]] bool dump_container(const std::vector<std::byte> &bytes,
                                  const fs::path &label, std::ostream &os) {
    const std::span<const std::byte> view{bytes.data(), bytes.size()};

    const rescore::EnigmaVersion version = rescore::detect_version(view);

    os << "== " << label.string() << " ==\n";
    os << "detected container: " << rescore::to_string(version.era);
    if (!version.version_string.empty()) {
        os << " (\"" << version.version_string << "\")";
    }
    os << (version.big_endian ? " [big-endian]" : " [little-endian]") << "\n";

    rescore::Diagnostics diags;
    const rescore::Result<rescore::container::RawDocument> doc =
        rescore::container::ContainerReader::parse(view, diags);
    if (doc.has_value()) {
        rescore::container::dump(doc.value(), view, os);
    } else {
        os << "  (could not parse container: " << doc.message() << ")\n";
    }

    // Surface any diagnostics so --dump honestly reflects what was and was not read.
    for (const rescore::Diagnostic &d : diags.all()) {
        const char *sev = d.severity == rescore::Severity::Error     ? "error"
                          : d.severity == rescore::Severity::Warning ? "warning"
                                                                     : "info";
        os << "  [" << sev << "] " << d.message << "\n";
    }
    os << "\n";
    return true;
}

// Run a single conversion. `dump_first` controls whether we also emit the
// container dump to stdout (honored even when conversion is unavailable).
// Writes MusicXML to `output` if set, otherwise to stdout. Returns the exit
// code for this single file.
[[nodiscard]] ExitCode convert_one(const fs::path &input,
                                   const std::optional<fs::path> &output,
                                   bool keep, bool dump_first) {
    std::string error;
    const std::optional<std::vector<std::byte>> maybe_bytes =
        read_file_bytes(input, error);
    if (!maybe_bytes.has_value()) {
        std::cerr << "rescore: " << error << "\n";
        return ExitCode::IoError;
    }
    const std::vector<std::byte> &bytes = *maybe_bytes;

    // --dump is honored before (and regardless of) conversion success.
    if (dump_first) {
        (void)dump_container(bytes, input, std::cout);
    }

    // The `keep` flag is accepted for forward-compatibility; the conversion
    // pipeline does not yet produce intermediate artifacts to keep.
    (void)keep;

    // .mxl output is a documented TODO: we still perform the conversion and
    // tell the user that compressed packaging is not wired up yet.
    const bool wants_mxl = output.has_value() && has_extension_ci(*output, ".mxl");

    const std::span<const std::byte> view{bytes.data(), bytes.size()};
    rescore::Diagnostics diags;
    const rescore::Result<std::string> conv =
        rescore::convert_mus_to_musicxml(view, diags);

    if (!conv.has_value()) {
        if (conv.code() == rescore::ErrorCode::NotImplemented) {
            std::cerr
                << "rescore: could not extract a score from '" << input.string()
                << "'.\n"
                   "  No measure-spec records were decoded - the content region may\n"
                   "  be empty, big-endian (early Macintosh), or use an unsupported\n"
                   "  encoding. Other .mus files may still convert. Re-run with\n"
                   "  --dump to inspect what was decoded.\n";
            return ExitCode::NotImplemented;
        }
        std::cerr << "rescore: conversion failed for '" << input.string()
                  << "': " << conv.message() << "\n";
        return ExitCode::IoError;
    }

    if (wants_mxl) {
        std::cerr << "rescore: compressed .mxl output is a documented TODO; "
                     "writing uncompressed MusicXML instead would change the\n"
                     "  requested extension, so nothing was written. Re-run "
                     "with a .musicxml output path for now.\n";
        return ExitCode::NotImplemented;
    }

    if (output.has_value()) {
        std::string write_error;
        if (!write_text_file(*output, conv.value(), write_error)) {
            std::cerr << "rescore: " << write_error << "\n";
            return ExitCode::IoError;
        }
    } else {
        std::cout << conv.value();
    }
    return ExitCode::Ok;
}

[[nodiscard]] ExitCode run_dump_only(const Options &opts) {
    std::string error;
    const std::optional<std::vector<std::byte>> maybe_bytes =
        read_file_bytes(*opts.input, error);
    if (!maybe_bytes.has_value()) {
        std::cerr << "rescore: " << error << "\n";
        return ExitCode::IoError;
    }
    (void)dump_container(*maybe_bytes, *opts.input, std::cout);
    return ExitCode::Ok;
}

[[nodiscard]] ExitCode run_batch(const Options &opts) {
    const fs::path &dir = *opts.batch_dir;
    const fs::path &outdir = *opts.output;

    std::error_code ec;
    if (!fs::exists(dir, ec) || ec || !fs::is_directory(dir, ec) || ec) {
        std::cerr << "rescore: --batch folder is not a directory: "
                  << dir.string() << "\n";
        return ExitCode::Usage;
    }

    fs::create_directories(outdir, ec);
    if (ec) {
        std::cerr << "rescore: could not create output directory: "
                  << outdir.string() << "\n";
        return ExitCode::IoError;
    }

    int total = 0;
    int converted = 0;
    int failed = 0;
    int not_impl = 0;

    // Iterate files directly in the folder (non-recursive); skip on error.
    fs::directory_iterator it(dir, ec);
    if (ec) {
        std::cerr << "rescore: could not read folder: " << dir.string() << "\n";
        return ExitCode::IoError;
    }
    for (const fs::directory_entry &entry : it) {
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec) || entry_ec) {
            continue;
        }
        if (!has_extension_ci(entry.path(), ".mus")) {
            continue;
        }
        ++total;

        fs::path out = outdir / entry.path().filename();
        out.replace_extension(".musicxml");

        const ExitCode code =
            convert_one(entry.path(), out, opts.keep, /*dump_first=*/opts.dump);
        switch (code) {
        case ExitCode::Ok:
            ++converted;
            std::cerr << "rescore: converted " << entry.path().filename().string()
                      << " -> " << out.filename().string() << "\n";
            break;
        case ExitCode::NotImplemented:
            ++not_impl;
            break;
        default:
            ++failed;
            break;
        }
    }

    std::cerr << "rescore: batch complete - " << total << " .mus file(s), "
              << converted << " converted, " << not_impl << " not yet supported, "
              << failed << " failed.\n";

    if (total == 0) {
        std::cerr << "rescore: no .mus files found in " << dir.string() << "\n";
    }

    // Batch is "successful" if it ran; per-file outcomes are reported above.
    // Return non-zero only if nothing converted but some files were eligible
    // and at least one hard-failed (I/O), so scripts can detect trouble.
    if (failed > 0 && converted == 0 && not_impl == 0) {
        return ExitCode::IoError;
    }
    return ExitCode::Ok;
}

[[nodiscard]] ExitCode run_emit_trivial_gold(const Options &opts) {
    // DEV path: build the canonical trivial gold score in the IR and push it
    // straight through the emitter. This proves the Layer 2 -> Layer 3 path
    // end to end without needing any .mus input.
    const rescore::ir::Score score = rescore::build_trivial_score();
    const rescore::Result<std::string> emitted =
        rescore::musicxml::emit_score_partwise(score);
    if (!emitted.has_value()) {
        std::cerr << "rescore: failed to emit trivial gold MusicXML: "
                  << emitted.message() << "\n";
        return ExitCode::IoError;
    }
    const std::string &musicxml = emitted.value();

    if (has_extension_ci(*opts.output, ".mxl")) {
        std::cerr << "rescore: compressed .mxl output is a documented TODO; "
                     "use a .musicxml output path for --emit-trivial-gold.\n";
        return ExitCode::NotImplemented;
    }

    std::string write_error;
    if (!write_text_file(*opts.output, musicxml, write_error)) {
        std::cerr << "rescore: " << write_error << "\n";
        return ExitCode::IoError;
    }
    std::cerr << "rescore: wrote trivial gold MusicXML to "
              << opts.output->string() << "\n";
    return ExitCode::Ok;
}

} // namespace

int main(int argc, char **argv) {
    const ParseResult parsed = parse_args(argc, argv);

    if (!parsed.options.has_value()) {
        std::cerr << "rescore: " << parsed.error << "\n\n";
        if (parsed.wants_help) {
            print_usage(std::cerr);
        }
        return to_int(ExitCode::Usage);
    }

    const Options &opts = *parsed.options;

    switch (opts.mode) {
    case Mode::Help:
        print_usage(std::cout);
        return to_int(ExitCode::Ok);

    case Mode::Version:
        print_version(std::cout);
        return to_int(ExitCode::Ok);

    case Mode::Dump:
        return to_int(run_dump_only(opts));

    case Mode::Batch:
        return to_int(run_batch(opts));

    case Mode::EmitTrivialGold:
        return to_int(run_emit_trivial_gold(opts));

    case Mode::Convert:
        return to_int(convert_one(*opts.input, opts.output, opts.keep,
                                  /*dump_first=*/opts.dump));
    }

    // Unreachable: every Mode is handled above.
    return to_int(ExitCode::Usage);
}
