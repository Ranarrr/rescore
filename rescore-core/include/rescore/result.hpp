// SPDX-License-Identifier: MIT
//
// result.hpp - Header-only, exception-free error handling primitives.
//
// The entire parse path is exception-free (untrusted archival input). Functions
// that can fail return a Result<T> which holds EITHER a value OR an error
// (message + code). Side-channel observations (recoverable warnings, parse
// notes) are accumulated in a Diagnostics sink threaded through the pipeline.
//
// This header has no dependencies beyond the standard library and is safe to
// include anywhere in the project.

#ifndef RESCORE_RESULT_HPP
#define RESCORE_RESULT_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rescore {

/// Severity of a diagnostic emitted while parsing or emitting.
enum class Severity { Info, Warning, Error };

/// Stable error categories for Result<T>. Kept small and append-only.
enum class ErrorCode {
    None = 0,
    InvalidArgument,
    UnexpectedEof,
    BadMagic,
    UnsupportedVersion,
    NotImplemented,
    MalformedData,
    EmitFailure,
    Unknown,
};

/// A single diagnostic message. `offset`, when present, is the byte offset into
/// the source buffer that the diagnostic pertains to (for `--dump`-style
/// reporting against an undocumented binary).
struct Diagnostic {
    Severity severity{Severity::Info};
    std::string message;
    std::optional<std::size_t> offset;
};

/// Accumulating diagnostic sink. Threaded by reference through the parse and
/// emit pipeline so that a malformed record can warn-and-continue rather than
/// abort. Never throws.
class Diagnostics {
public:
    void add(Severity severity, std::string message,
             std::optional<std::size_t> offset = std::nullopt) {
        entries_.push_back(Diagnostic{severity, std::move(message), offset});
    }

    void info(std::string message, std::optional<std::size_t> offset = std::nullopt) {
        add(Severity::Info, std::move(message), offset);
    }

    void warn(std::string message, std::optional<std::size_t> offset = std::nullopt) {
        add(Severity::Warning, std::move(message), offset);
    }

    void error(std::string message, std::optional<std::size_t> offset = std::nullopt) {
        add(Severity::Error, std::move(message), offset);
    }

    /// All diagnostics in insertion order.
    [[nodiscard]] const std::vector<Diagnostic>& all() const noexcept { return entries_; }

    /// Diagnostics at Warning severity.
    [[nodiscard]] std::vector<Diagnostic> warnings() const {
        return filter(Severity::Warning);
    }

    /// Diagnostics at Error severity.
    [[nodiscard]] std::vector<Diagnostic> errors() const { return filter(Severity::Error); }

    /// True if any Error-severity diagnostic has been recorded.
    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto& d : entries_) {
            if (d.severity == Severity::Error) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    [[nodiscard]] std::vector<Diagnostic> filter(Severity severity) const {
        std::vector<Diagnostic> out;
        for (const auto& d : entries_) {
            if (d.severity == severity) {
                out.push_back(d);
            }
        }
        return out;
    }

    std::vector<Diagnostic> entries_;
};

/// The error half of a Result<T>.
struct Error {
    ErrorCode code{ErrorCode::Unknown};
    std::string message;
};

/// Result<T> holds EITHER a value of type T OR an Error. No exceptions are used
/// for control flow anywhere in the codebase; callers inspect has_value().
///
/// Construct success with a T (implicit) or Result<T>::ok(...); construct
/// failure with Result<T>::fail(code, message) or by assigning an Error.
template <typename T>
class Result {
public:
    Result(T value) : storage_(std::move(value)) {}            // NOLINT(*-explicit*)
    Result(Error error) : storage_(std::move(error)) {}        // NOLINT(*-explicit*)

    static Result ok(T value) { return Result(std::move(value)); }
    static Result fail(ErrorCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(storage_);
    }
    explicit operator bool() const noexcept { return has_value(); }

    /// Precondition: has_value(). Returns the contained value.
    [[nodiscard]] T& value() & { return std::get<T>(storage_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(storage_)); }

    /// Precondition: !has_value(). Returns the contained error.
    [[nodiscard]] const Error& error() const& { return std::get<Error>(storage_); }

    [[nodiscard]] ErrorCode code() const {
        return has_value() ? ErrorCode::None : std::get<Error>(storage_).code;
    }

    [[nodiscard]] const std::string& message() const {
        static const std::string kEmpty;
        return has_value() ? kEmpty : std::get<Error>(storage_).message;
    }

    /// Returns the value if present, otherwise `fallback`.
    [[nodiscard]] T value_or(T fallback) const& {
        return has_value() ? std::get<T>(storage_) : std::move(fallback);
    }

private:
    std::variant<T, Error> storage_;
};

} // namespace rescore

#endif // RESCORE_RESULT_HPP
