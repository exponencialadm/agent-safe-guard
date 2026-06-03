// Adversarial unit tests for sg::output_filter.
//
// Run under ASan + UBSan via `make test-memory`. docs/memory-safety.md
// requires every PR that allocates in inner loops to ship with adversarial
// cases; this file satisfies that contract for PostToolUse filters.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "sg/output_filter.hpp"

#include <chrono>
#include <cstddef>
#include <sstream>
#include <string>

namespace {

std::string Repeat(const std::string& part, std::size_t n) {
  std::string out;
  out.reserve(part.size() * n);
  for (std::size_t i = 0; i < n; ++i) {
    out.append(part);
  }
  return out;
}

std::string MakeVerboseStatus(std::size_t modified_files) {
  std::ostringstream o;
  o << "On branch main\n";
  o << "Your branch is up to date with 'origin/main'.\n\n";
  o << "Changes not staged for commit:\n";
  o << "  (use \"git add <file>...\" to update what will be committed)\n";
  o << "  (use \"git restore <file>...\" to discard changes in working "
       "directory)\n";
  for (std::size_t i = 0; i < modified_files; ++i) {
    o << "\tmodified:   src/file" << i << ".cpp\n";
  }
  o << "\nno changes added to commit (use \"git add\" and/or \"git commit "
       "-a\")\n";
  return o.str();
}

}  // namespace

// ===========================================================================
// IsGitStatusCommand
// ===========================================================================

TEST_CASE("IsGitStatusCommand: positive cases") {
  CHECK(sg::IsGitStatusCommand("git status"));
  CHECK(sg::IsGitStatusCommand("  git status"));
  CHECK(sg::IsGitStatusCommand("git\tstatus"));
  CHECK(sg::IsGitStatusCommand("git status -uno"));
  CHECK(sg::IsGitStatusCommand("git status --porcelain"));
  CHECK(sg::IsGitStatusCommand("git status -s"));
  CHECK(sg::IsGitStatusCommand("git  status"));
}

TEST_CASE("IsGitStatusCommand: negative cases") {
  CHECK_FALSE(sg::IsGitStatusCommand(""));
  CHECK_FALSE(sg::IsGitStatusCommand("git stash"));
  CHECK_FALSE(sg::IsGitStatusCommand("git statuses"));
  CHECK_FALSE(sg::IsGitStatusCommand("cd /tmp && git status"));
  CHECK_FALSE(sg::IsGitStatusCommand("echo git status"));
  CHECK_FALSE(sg::IsGitStatusCommand("gitstatus"));
  CHECK_FALSE(sg::IsGitStatusCommand("git-status"));
}

// ===========================================================================
// OutputLooksLikeFailure
// ===========================================================================

TEST_CASE("OutputLooksLikeFailure: detects line-anchored failure markers") {
  CHECK(sg::OutputLooksLikeFailure("fatal: not a git repository"));
  CHECK(sg::OutputLooksLikeFailure("error: pathspec did not match"));
  CHECK(sg::OutputLooksLikeFailure("usage: git status [<options>]"));
  CHECK(sg::OutputLooksLikeFailure("git: 'stat' is not a git command"));
  CHECK(sg::OutputLooksLikeFailure(
      "On branch main\nfatal: bad index file\n"));
  CHECK(sg::OutputLooksLikeFailure("  fatal:   indented"));
}

TEST_CASE("OutputLooksLikeFailure: ignores mid-line and look-alike substrings") {
  CHECK_FALSE(sg::OutputLooksLikeFailure(""));
  CHECK_FALSE(sg::OutputLooksLikeFailure("On branch main"));
  CHECK_FALSE(sg::OutputLooksLikeFailure(
      "modified: src/fatal.cpp"));  // filename contains 'fatal'
  CHECK_FALSE(sg::OutputLooksLikeFailure("a fatal: mid-sentence b"));
  CHECK_FALSE(sg::OutputLooksLikeFailure(
      "Untracked files:\n\terrors.log\n"));  // filename starts 'errors'
}

// ===========================================================================
// FilterGitStatusVerbose — happy paths
// ===========================================================================

TEST_CASE("FilterGitStatusVerbose: strips standalone advice lines") {
  std::string input =
      "On branch main\n"
      "Changes not staged for commit:\n"
      "  (use \"git add <file>...\" to update what will be committed)\n"
      "  (use \"git restore <file>...\" to discard changes)\n"
      "\tmodified:   foo.cpp\n";
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose(input, &out));
  CHECK(out.find("(use \"git add") == std::string::npos);
  CHECK(out.find("(use \"git restore") == std::string::npos);
  CHECK(out.find("modified:   foo.cpp") != std::string::npos);
}

TEST_CASE("FilterGitStatusVerbose: strips trailing parenthetical advice") {
  std::string input =
      "On branch main\n"
      "no changes added to commit (use \"git add\" and/or \"git commit -a\")"
      "\n";
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose(input, &out));
  CHECK(out.find("(use ") == std::string::npos);
  CHECK(out.find("no changes added to commit") != std::string::npos);
}

TEST_CASE("FilterGitStatusVerbose: collapses runs of blank lines") {
  std::string input = "On branch main\n\n\n\nChanges:\n\n\nfoo\n";
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose(input, &out));
  // Should have at most single blank-line runs.
  CHECK(out.find("\n\n\n") == std::string::npos);
}

TEST_CASE("FilterGitStatusVerbose: empty input returns true with empty out") {
  std::string out = "garbage";  // make sure it gets overwritten
  REQUIRE(sg::FilterGitStatusVerbose("", &out));
  CHECK(out.empty());
}

TEST_CASE("FilterGitStatusVerbose: blank-only input returns empty") {
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose("\n\n   \n\n", &out));
  CHECK(out.empty());
}

// ===========================================================================
// FilterGitStatusVerbose — cap path
// ===========================================================================

TEST_CASE("FilterGitStatusVerbose: caps long output with elided marker") {
  std::string input = MakeVerboseStatus(/*modified_files=*/200);
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose(input, &out));
  CHECK(out.find("elided") != std::string::npos);
  CHECK(out.find("file0.cpp") != std::string::npos);    // head
  CHECK(out.find("file199.cpp") != std::string::npos);  // tail
  // Cap is kGitStatusMaxKeptLines=100. Counting newlines in `out` is a
  // proxy for kept-line count.
  std::size_t newlines = 0;
  for (char c : out) {
    if (c == '\n') {
      ++newlines;
    }
  }
  CHECK(newlines <= sg::kGitStatusHeadLines + sg::kGitStatusTailLines + 4);
}

// ===========================================================================
// FilterGitStatusVerbose — bounded growth (the 2026-05-01 contract)
// ===========================================================================

TEST_CASE("FilterGitStatusVerbose: refuses input with > kMaxLines lines") {
  // One byte per line (`\n` only) exceeds the line cap.
  std::string input(sg::kGitStatusFilterMaxLines + 10, '\n');
  std::string out;
  CHECK_FALSE(sg::FilterGitStatusVerbose(input, &out));
}

TEST_CASE("FilterGitStatusVerbose: 50k legal lines completes under 5s") {
  // Just inside the line cap — must succeed without timing out.
  const std::size_t n = sg::kGitStatusFilterMaxLines - 100;
  std::string input;
  input.reserve(n * 16);
  for (std::size_t i = 0; i < n; ++i) {
    input.append("\tmodified:   x.c\n");
  }
  std::string out;
  const auto start = std::chrono::steady_clock::now();
  REQUIRE(sg::FilterGitStatusVerbose(input, &out));
  const auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 5);
  // Cap should kick in.
  CHECK(out.find("elided") != std::string::npos);
}

TEST_CASE("FilterGitStatusVerbose: pathological hint line does not blow up") {
  // 10k consecutive advice lines — must be O(N), not super-linear.
  std::string input = Repeat(
      "  (use \"git add <file>...\" to update what will be committed)\n",
      10000);
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose(input, &out));
  CHECK(out.empty());  // every line stripped
}

TEST_CASE("FilterGitStatusVerbose: NUL bytes survive without crash") {
  std::string input = "On branch main\nfoo\0bar\n\tmodified: x\n";
  // The literal above stops at the NUL — build it explicitly:
  std::string built;
  built.append("On branch main\n");
  built.append(1, '\0');
  built.append("middle\n");
  built.append("\tmodified: x\n");
  std::string out;
  REQUIRE(sg::FilterGitStatusVerbose(built, &out));
  CHECK(out.find("modified: x") != std::string::npos);
}

// ===========================================================================
// MaybeApplyGitStatusFilter — orchestration
// ===========================================================================

TEST_CASE("MaybeApplyGitStatusFilter: rejects non-matching commands") {
  std::string out = "anything";
  CHECK_FALSE(sg::MaybeApplyGitStatusFilter("echo hi", &out));
  CHECK(out == "anything");  // unchanged
}

TEST_CASE("MaybeApplyGitStatusFilter: rejects oversized output") {
  std::string output(sg::kGitStatusFilterMaxBytes + 1, 'x');
  std::string copy = output;
  CHECK_FALSE(sg::MaybeApplyGitStatusFilter("git status", &output));
  CHECK(output == copy);
}

TEST_CASE("MaybeApplyGitStatusFilter: tee on failure preserves raw output") {
  std::string output =
      "On branch main\n"
      "  (use \"git add <file>...\")\n"
      "fatal: bad index file\n";
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyGitStatusFilter("git status", &output));
  CHECK(output == original);
}

TEST_CASE("MaybeApplyGitStatusFilter: applies filter and reports reduction") {
  std::string output = MakeVerboseStatus(/*modified_files=*/5);
  const std::size_t before = output.size();
  CHECK(sg::MaybeApplyGitStatusFilter("git status", &output));
  CHECK(output.size() < before);
  CHECK(output.find("(use \"git add") == std::string::npos);
  CHECK(output.find("(use \"git restore") == std::string::npos);
}

TEST_CASE("MaybeApplyGitStatusFilter: returns false when no reduction") {
  // Porcelain-style output has no noise — filter is a no-op.
  std::string output = " M src/foo.cpp\n?? docs/new.md\n";
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyGitStatusFilter("git status --porcelain", &output));
  CHECK(output == original);
}

TEST_CASE("MaybeApplyGitStatusFilter: empty output is left alone") {
  std::string output;
  CHECK_FALSE(sg::MaybeApplyGitStatusFilter("git status", &output));
  CHECK(output.empty());
}

// ===========================================================================
// IsPytestCommand
// ===========================================================================

TEST_CASE("IsPytestCommand: positive cases") {
  CHECK(sg::IsPytestCommand("pytest"));
  CHECK(sg::IsPytestCommand("pytest tests/"));
  CHECK(sg::IsPytestCommand("pytest -v -k foo"));
  CHECK(sg::IsPytestCommand("py.test"));
  CHECK(sg::IsPytestCommand("python -m pytest"));
  CHECK(sg::IsPytestCommand("python3 -m pytest tests/"));
  CHECK(sg::IsPytestCommand("python3.12 -m pytest"));
  CHECK(sg::IsPytestCommand("  pytest"));
}

TEST_CASE("IsPytestCommand: negative cases") {
  CHECK_FALSE(sg::IsPytestCommand(""));
  CHECK_FALSE(sg::IsPytestCommand("pytest-bdd"));
  CHECK_FALSE(sg::IsPytestCommand("pytest-cov"));
  CHECK_FALSE(sg::IsPytestCommand("echo pytest"));
  CHECK_FALSE(sg::IsPytestCommand("cd /tmp && pytest"));
  CHECK_FALSE(sg::IsPytestCommand("python -c 'import pytest'"));
  CHECK_FALSE(sg::IsPytestCommand("pytestify"));
}

// ===========================================================================
// OutputLooksLikePytestInternalError
// ===========================================================================

TEST_CASE("OutputLooksLikePytestInternalError: line-anchored detection") {
  CHECK(sg::OutputLooksLikePytestInternalError(
      "INTERNALERROR> Traceback (most recent call last):"));
  CHECK(sg::OutputLooksLikePytestInternalError(
      "===== test session starts =====\n"
      "INTERNALERROR> something\n"));
  CHECK_FALSE(sg::OutputLooksLikePytestInternalError(
      "this line says INTERNALERROR> but mid-line"));
  CHECK_FALSE(sg::OutputLooksLikePytestInternalError(""));
  CHECK_FALSE(sg::OutputLooksLikePytestInternalError(
      "Test failed but no internal error here"));
}

// ===========================================================================
// FilterPytestVerbose — happy paths
// ===========================================================================

TEST_CASE("FilterPytestVerbose: strips canonical PASSED lines") {
  std::string input =
      "===== test session starts =====\n"
      "tests/test_a.py::test_one PASSED                          [ 25%]\n"
      "tests/test_a.py::test_two PASSED                          [ 50%]\n"
      "tests/test_a.py::test_three FAILED                        [ 75%]\n"
      "===== 1 failed, 2 passed in 0.05s =====\n";
  std::string out;
  REQUIRE(sg::FilterPytestVerbose(input, &out));
  CHECK(out.find("test_one PASSED") == std::string::npos);
  CHECK(out.find("test_two PASSED") == std::string::npos);
  CHECK(out.find("test_three FAILED") != std::string::npos);
  CHECK(out.find("test session starts") != std::string::npos);
  CHECK(out.find("1 failed, 2 passed") != std::string::npos);
}

TEST_CASE("FilterPytestVerbose: keeps SKIPPED, ERROR, XFAIL, XPASS") {
  std::string input =
      "tests/test_a.py::test_one SKIPPED (reason)                [ 25%]\n"
      "tests/test_a.py::test_two ERROR                           [ 50%]\n"
      "tests/test_a.py::test_three XFAIL                         [ 75%]\n"
      "tests/test_a.py::test_four XPASSED                        [100%]\n";
  std::string out;
  REQUIRE(sg::FilterPytestVerbose(input, &out));
  // None of these are stripped.
  CHECK(out.find("test_one SKIPPED") != std::string::npos);
  CHECK(out.find("test_two ERROR") != std::string::npos);
  CHECK(out.find("test_three XFAIL") != std::string::npos);
  CHECK(out.find("test_four XPASSED") != std::string::npos);
}

TEST_CASE("FilterPytestVerbose: preserves trailing newline of input") {
  std::string input = "tests/x.py::a PASSED [ 50%]\nsummary line\n";
  std::string out;
  REQUIRE(sg::FilterPytestVerbose(input, &out));
  CHECK(out.back() == '\n');
}

TEST_CASE("FilterPytestVerbose: empty input is no-op") {
  std::string out = "garbage";
  REQUIRE(sg::FilterPytestVerbose("", &out));
  CHECK(out.empty());
}

// ===========================================================================
// FilterPytestVerbose — bounded growth
// ===========================================================================

TEST_CASE("FilterPytestVerbose: refuses input exceeding line cap") {
  std::string input(sg::kPytestFilterMaxLines + 10, '\n');
  std::string out;
  CHECK_FALSE(sg::FilterPytestVerbose(input, &out));
}

TEST_CASE("FilterPytestVerbose: 100k PASSED lines compress to summary") {
  // Just inside the line cap. Each PASSED line is stripped.
  const std::size_t n = sg::kPytestFilterMaxLines - 100;
  std::string input;
  input.reserve(n * 60);
  for (std::size_t i = 0; i < n; ++i) {
    input.append("tests/x.py::t PASSED [ 50%]\n");
  }
  input.append("===== ");
  input.append(std::to_string(n));
  input.append(" passed in 0.5s =====\n");
  std::string out;
  REQUIRE(sg::FilterPytestVerbose(input, &out));
  // Output should be much smaller than input (only the summary line
  // remains).
  CHECK(out.size() < input.size() / 100);
  CHECK(out.find("passed in 0.5s") != std::string::npos);
}

// ===========================================================================
// MaybeApplyPytestFilter
// ===========================================================================

TEST_CASE("MaybeApplyPytestFilter: rejects non-matching commands") {
  std::string output = "tests/x.py::a PASSED [ 50%]\n";
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyPytestFilter("git log", &output));
  CHECK(output == original);
}

TEST_CASE("MaybeApplyPytestFilter: tee on INTERNALERROR") {
  std::string output =
      "tests/x.py::a PASSED [ 50%]\n"
      "INTERNALERROR> pytest crashed\n";
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyPytestFilter("pytest", &output));
  CHECK(output == original);
}

TEST_CASE("MaybeApplyPytestFilter: applies and reduces") {
  std::string output =
      "===== test session starts =====\n"
      "tests/test_a.py::test_one PASSED [ 33%]\n"
      "tests/test_a.py::test_two PASSED [ 66%]\n"
      "tests/test_a.py::test_three PASSED [100%]\n"
      "===== 3 passed in 0.05s =====\n";
  const std::size_t before = output.size();
  CHECK(sg::MaybeApplyPytestFilter("pytest -v", &output));
  CHECK(output.size() < before);
  CHECK(output.find("PASSED") == std::string::npos);
  CHECK(output.find("3 passed") != std::string::npos);
}

TEST_CASE("MaybeApplyPytestFilter: oversized output rejected") {
  std::string output(sg::kPytestFilterMaxBytes + 1, 'x');
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyPytestFilter("pytest", &output));
  CHECK(output == original);
}

TEST_CASE("MaybeApplyPytestFilter: no PASSED to strip → declines") {
  std::string output =
      "===== test session starts =====\n"
      "tests/x.py::a FAILED [ 50%]\n"
      "tests/x.py::b FAILED [100%]\n"
      "===== 2 failed in 0.05s =====\n";
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyPytestFilter("pytest", &output));
  CHECK(output == original);
}

// ===========================================================================
// MaybeApplyOutputFilter — dispatcher
// ===========================================================================

TEST_CASE("MaybeApplyOutputFilter: dispatches to git_status") {
  std::string output =
      "On branch main\n"
      "Changes not staged for commit:\n"
      "  (use \"git add <file>...\" to update what will be committed)\n"
      "\tmodified: foo\n";
  const std::size_t before = output.size();
  CHECK(sg::MaybeApplyOutputFilter("git status", &output));
  CHECK(output.size() < before);
}

TEST_CASE("MaybeApplyOutputFilter: dispatches to pytest") {
  std::string output =
      "===== test session starts =====\n"
      "tests/x.py::a PASSED [100%]\n"
      "===== 1 passed in 0.01s =====\n";
  const std::size_t before = output.size();
  CHECK(sg::MaybeApplyOutputFilter("pytest", &output));
  CHECK(output.size() < before);
}

TEST_CASE("MaybeApplyOutputFilter: returns false for unknown commands") {
  std::string output = "anything";
  const std::string original = output;
  CHECK_FALSE(sg::MaybeApplyOutputFilter("ls -la", &output));
  CHECK(output == original);
}
