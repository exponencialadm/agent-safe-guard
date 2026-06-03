#include "sg/output_filter.hpp"

#include <cctype>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace sg {
namespace {

std::string Trim(const std::string& input) {
  std::size_t first = 0;
  while (first < input.size() &&
         std::isspace(static_cast<unsigned char>(input[first])) != 0) {
    ++first;
  }
  std::size_t last = input.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(input[last - 1])) != 0) {
    --last;
  }
  return input.substr(first, last - first);
}

}  // namespace

bool IsGitStatusCommand(const std::string& command) {
  static const std::regex re(R"(^\s*git\s+status\b)");
  return std::regex_search(command, re);
}

bool OutputLooksLikeFailure(const std::string& output) {
  static const std::regex re(
      R"((^|\n)\s*(fatal:|error:|usage:|git: '))");
  return std::regex_search(output, re);
}

bool FilterGitStatusVerbose(const std::string& input, std::string* out) {
  static const std::regex hint_re(R"(^\s*\(use ")");
  static const std::regex trail_use_re(
      R"(\s*\(use "[^"]*"[^)]*\)\s*$)");

  std::vector<std::string> kept;
  std::istringstream is(input);
  std::string line;
  bool prev_blank = false;
  std::size_t total_lines = 0;
  while (std::getline(is, line)) {
    if (++total_lines > kGitStatusFilterMaxLines) {
      return false;
    }
    if (std::regex_search(line, hint_re)) {
      continue;
    }
    line = std::regex_replace(line, trail_use_re, "");
    const bool is_blank = Trim(line).empty();
    if (is_blank && prev_blank) {
      continue;
    }
    prev_blank = is_blank;
    kept.push_back(std::move(line));
  }

  if (kept.size() > kGitStatusMaxKeptLines) {
    const std::size_t elided =
        kept.size() - kGitStatusHeadLines - kGitStatusTailLines;
    std::vector<std::string> capped;
    capped.reserve(kGitStatusHeadLines + kGitStatusTailLines + 1);
    capped.insert(capped.end(),
                  std::make_move_iterator(kept.begin()),
                  std::make_move_iterator(kept.begin() +
                                          kGitStatusHeadLines));
    capped.push_back("... [" + std::to_string(elided) +
                     " lines elided] ...");
    capped.insert(
        capped.end(),
        std::make_move_iterator(kept.end() - kGitStatusTailLines),
        std::make_move_iterator(kept.end()));
    kept = std::move(capped);
  }

  std::ostringstream o;
  for (std::size_t i = 0; i < kept.size(); ++i) {
    if (i > 0) {
      o << '\n';
    }
    o << kept[i];
  }
  *out = o.str();
  // Preserve a trailing newline only when the filtered content is non-empty
  // and the input had one — keeps no-op porcelain inputs byte-identical so
  // the size-reduction guard correctly declines to apply the filter. An
  // empty filtered output stays empty (caller treats it as "nothing kept").
  if (!out->empty() && !input.empty() && input.back() == '\n' &&
      out->back() != '\n') {
    out->push_back('\n');
  }
  return true;
}

bool MaybeApplyGitStatusFilter(const std::string& command,
                               std::string* output) {
  if (!IsGitStatusCommand(command)) {
    return false;
  }
  if (output->size() > kGitStatusFilterMaxBytes) {
    return false;
  }
  if (OutputLooksLikeFailure(*output)) {
    return false;
  }
  std::string filtered;
  if (!FilterGitStatusVerbose(*output, &filtered)) {
    return false;
  }
  if (filtered.empty() || filtered.size() >= output->size()) {
    return false;
  }
  *output = std::move(filtered);
  return true;
}

// --- pytest verbose filter -------------------------------------------------

bool IsPytestCommand(const std::string& command) {
  // `python[\d.]*` covers `python`, `python3`, `python3.12`, etc., while
  // still rejecting look-alikes like `pythonista -m pytest`.
  static const std::regex re(
      R"(^\s*(pytest|py\.test|python[\d.]*\s+-m\s+pytest)(\s|$))");
  return std::regex_search(command, re);
}

bool OutputLooksLikePytestInternalError(const std::string& output) {
  static const std::regex re(R"((^|\n)INTERNALERROR>)");
  return std::regex_search(output, re);
}

bool FilterPytestVerbose(const std::string& input, std::string* out) {
  // Match the canonical per-test PASSED line in `pytest -v` output, e.g.
  //   `tests/test_x.py::test_one PASSED                          [ 12%]`.
  // The leading whitespace + literal PASSED + percentage suffix are the
  // discriminator — we deliberately do NOT match bare "PASSED" tokens
  // that show up in commit messages, log lines, or human prose.
  static const std::regex pass_re(R"(\sPASSED\s+\[\s*\d+%\])");

  std::ostringstream o;
  std::istringstream is(input);
  std::string line;
  bool first = true;
  std::size_t total_lines = 0;
  while (std::getline(is, line)) {
    if (++total_lines > kPytestFilterMaxLines) {
      return false;
    }
    if (std::regex_search(line, pass_re)) {
      continue;
    }
    if (!first) {
      o << '\n';
    }
    o << line;
    first = false;
  }
  *out = o.str();
  if (!out->empty() && !input.empty() && input.back() == '\n' &&
      out->back() != '\n') {
    out->push_back('\n');
  }
  return true;
}

bool MaybeApplyPytestFilter(const std::string& command,
                            std::string* output) {
  if (!IsPytestCommand(command)) {
    return false;
  }
  if (output->size() > kPytestFilterMaxBytes) {
    return false;
  }
  if (OutputLooksLikePytestInternalError(*output)) {
    return false;
  }
  std::string filtered;
  if (!FilterPytestVerbose(*output, &filtered)) {
    return false;
  }
  if (filtered.empty() || filtered.size() >= output->size()) {
    return false;
  }
  *output = std::move(filtered);
  return true;
}

// --- dispatcher ------------------------------------------------------------

bool MaybeApplyOutputFilter(const std::string& command,
                            std::string* output) {
  if (MaybeApplyGitStatusFilter(command, output)) {
    return true;
  }
  if (MaybeApplyPytestFilter(command, output)) {
    return true;
  }
  return false;
}

}  // namespace sg
