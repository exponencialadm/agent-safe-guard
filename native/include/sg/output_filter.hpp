#pragma once

#include <cstddef>
#include <string>

namespace sg {

// Phase A built-in PostToolUse output transforms.
//
// All functions here are bounded — see docs/memory-safety.md.
// Callers are responsible for time/size budgets on what they hand in; the
// filter never amplifies and refuses inputs above its declared caps.

inline constexpr std::size_t kGitStatusFilterMaxBytes = 1u * 1024 * 1024;
inline constexpr std::size_t kGitStatusFilterMaxLines = 50000;
inline constexpr std::size_t kGitStatusMaxKeptLines = 100;
inline constexpr std::size_t kGitStatusHeadLines = 70;
inline constexpr std::size_t kGitStatusTailLines = 25;

// True iff `command` starts with `git status` (whitespace-tolerant).
bool IsGitStatusCommand(const std::string& command);

// True iff `output` contains a line beginning (after optional whitespace)
// with `fatal:` / `error:` / `usage:` / `git: '`. Used for tee-on-failure:
// when true, the caller MUST pass the raw output through unchanged.
bool OutputLooksLikeFailure(const std::string& output);

// Strip `(use "...")` advice lines and trailing parenthetical advice from
// `git status` verbose output, collapsing runs of blank lines. Caps the
// kept-line count and inserts an `... [N lines elided] ...` marker.
//
// Returns false (and leaves `*out` undefined) only on the bounded-line
// guard — input with more than kGitStatusFilterMaxLines lines is refused.
// Returns true with `*out` set otherwise.
bool FilterGitStatusVerbose(const std::string& input, std::string* out);

// Top-level entry point for the git-status transform. Returns true and
// overwrites `*output` with the filtered text only when:
//   - `command` matches IsGitStatusCommand,
//   - `*output` is at most kGitStatusFilterMaxBytes,
//   - OutputLooksLikeFailure is false (tee on failure),
//   - FilterGitStatusVerbose succeeded and reduced the output size.
// In every other case the function returns false and leaves `*output`
// untouched, so the caller can fall through to its normal pipeline.
bool MaybeApplyGitStatusFilter(const std::string& command,
                               std::string* output);

// ---------------------------------------------------------------------------
// pytest verbose filter (Phase B.1)
//
// Strips per-test PASSED lines from `pytest -v` output, leaving FAILED,
// ERROR, SKIPPED, traceback, and summary lines intact. Tee on failure when
// the output begins or contains an `INTERNALERROR>`-prefixed line — that
// signals pytest itself crashed, and the user must see the raw output.

inline constexpr std::size_t kPytestFilterMaxBytes = 4u * 1024 * 1024;
inline constexpr std::size_t kPytestFilterMaxLines = 100000;

bool IsPytestCommand(const std::string& command);
bool OutputLooksLikePytestInternalError(const std::string& output);
bool FilterPytestVerbose(const std::string& input, std::string* out);
bool MaybeApplyPytestFilter(const std::string& command, std::string* output);

// ---------------------------------------------------------------------------
// Dispatcher

// Tries each registered filter in order; stops at the first one that
// applies. Returns true iff some filter modified `*output`.
bool MaybeApplyOutputFilter(const std::string& command, std::string* output);

}  // namespace sg
