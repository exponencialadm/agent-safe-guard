#!/usr/bin/env bats
# Phase A: Bash output filters — git status verbose form
#
# These tests drive the implementation of a built-in PostToolUse transform
# that strips noise lines (`(use "...")`) from `git status` verbose output
# and caps very long outputs. Tee-on-failure: outputs containing
# `fatal:` / `error:` / `usage:` are passed through unchanged.

setup() {
    load '../test_helper/common'
    setup_isolated_env
    # Native-only feature. The legacy bash reference hook (used by the
    # default `make test` bats run) does not implement this transform.
    # Tests are exercised via `make test-native-output-filter-smoke`.
    [ -n "${SG_DAEMON_SOCKET:-}" ] || skip "native daemon required (use make test-native-output-filter-smoke)"
    HOOK="${SG_POST_TOOL_HOOK:-$PROJ_ROOT/build/native/native/sg-hook-post-tool-use}"
}

teardown() {
    teardown_isolated_env
}

# ---------------------------------------------------------------------------
# Helpers

make_verbose_git_status() {
    cat <<'EOF'
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   src/foo.cpp
	modified:   src/bar.cpp
	modified:   include/baz.hpp
	modified:   tests/test1.cpp

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/new.md
	scripts/extra.sh

no changes added to commit (use "git add" and/or "git commit -a")
EOF
}

# ---------------------------------------------------------------------------
# Happy path: strip "(use ..." hints

@test "git status verbose: strips '(use ...)' hint lines" {
    local text
    text=$(make_verbose_git_status)
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "git status"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    assert_output --partial "modifyOutput"
    # Hint lines must be gone (substring check; JSON escapes only inner quotes).
    refute_output --partial "(use "
    # File lines preserved.
    assert_output --partial "src/foo.cpp"
    assert_output --partial "docs/new.md"
}

# ---------------------------------------------------------------------------
# Tee on failure: fatal: short-circuits the transform

@test "git status with fatal: in output is passed through (tee on failure)" {
    # Construct an output that looks like git status verbose but contains
    # `fatal:` near the end. Without tee logic, the filter would strip
    # `(use ...)` and return modifyOutput. With tee, we must skip the
    # transform and let the raw output flow.
    local text
    text=$(printf '%s\n' \
        'On branch main' \
        'Changes not staged for commit:' \
        '  (use "git add <file>..." to update what will be committed)' \
        $'\tmodified:   foo.cpp' \
        'fatal: bad index file: .git/index')
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "git status"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    # No modifyOutput → the filter declined to transform.
    refute_output --partial "modifyOutput"
    assert_output --partial "suppressOutput"
}

# ---------------------------------------------------------------------------
# Cap: very long file list gets capped with elided marker

@test "git status caps long output and inserts elided marker" {
    local text
    text=$(python3 - <<'PY'
lines = ["On branch main", "Changes not staged for commit:"]
for i in range(200):
    lines.append(f"\tmodified:   src/file{i}.cpp")
print("\n".join(lines))
PY
)
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "git status"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    assert_output --partial "modifyOutput"
    assert_output --partial "elided"
    # First and last file lines should both be present (head + tail kept).
    assert_output --partial "src/file0.cpp"
    assert_output --partial "src/file199.cpp"
}

# ---------------------------------------------------------------------------
# Non-matching command: filter does NOT trigger for non-git-status

@test "non-git-status Bash command is not affected by the filter" {
    # Even if the output contains '(use ...)' verbatim, a non-matching
    # command must not trigger the git-status transform.
    local text
    text=$'  (use "git add <file>..." to update what will be committed)\nsome echo output'
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "echo hi"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    refute_output --partial "modifyOutput"
    assert_output --partial "suppressOutput"
}

# ---------------------------------------------------------------------------
# Porcelain mode: no noise, transform should be a no-op (no modifyOutput)

@test "git status --porcelain is not modified (no noise to strip)" {
    local text
    text=$(printf '%s\n' \
        ' M src/foo.cpp' \
        ' M src/bar.cpp' \
        '?? docs/new.md')
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "git status --porcelain"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    refute_output --partial "modifyOutput"
    assert_output --partial "suppressOutput"
}

# ===========================================================================
# pytest verbose filter — Phase B.1
# ===========================================================================

make_pytest_verbose_passing() {
    python3 - <<'PY'
out = [
    "============================= test session starts ==============================",
    "platform linux -- Python 3.12.3, pytest-7.4.0, pluggy-1.0.0",
    "rootdir: /tmp/myproj",
    "collected 30 items",
    "",
]
for i in range(30):
    out.append(f"tests/test_module.py::test_func_{i:03d} PASSED                          [{(i+1)*100//30:3d}%]")
out.append("")
out.append("============================== 30 passed in 0.42s ==============================")
print("\n".join(out))
PY
}

@test "pytest verbose: strips PASSED lines from passing run" {
    local text
    text=$(make_pytest_verbose_passing)
    local before=${#text}
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "pytest -v"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    assert_output --partial "modifyOutput"
    refute_output --partial "test_func_001 PASSED"
    refute_output --partial "test_func_029 PASSED"
    # Summary must still be there.
    assert_output --partial "30 passed in 0.42s"
    # Test session header must still be there.
    assert_output --partial "test session starts"
}

@test "pytest verbose: mixed pass/fail keeps FAILED + tracebacks" {
    local text
    text=$(cat <<'EOF'
============================= test session starts ==============================
platform linux -- Python 3.12.3, pytest-7.4.0, pluggy-1.0.0
collected 4 items

tests/test_x.py::test_one PASSED                                          [ 25%]
tests/test_x.py::test_two FAILED                                          [ 50%]
tests/test_x.py::test_three PASSED                                        [ 75%]
tests/test_x.py::test_four PASSED                                         [100%]

=================================== FAILURES ===================================
___________________________________ test_two ___________________________________

    def test_two():
>       assert 1 == 2
E       assert 1 == 2

tests/test_x.py:5: AssertionError
=========================== short test summary info ============================
FAILED tests/test_x.py::test_two - assert 1 == 2
========================= 1 failed, 3 passed in 0.05s ==========================
EOF
)
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "pytest -v tests/"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    assert_output --partial "modifyOutput"
    # FAILED line and traceback preserved.
    assert_output --partial "test_two FAILED"
    assert_output --partial "AssertionError"
    assert_output --partial "1 failed, 3 passed"
    # PASSED lines stripped.
    refute_output --partial "test_one PASSED"
    refute_output --partial "test_three PASSED"
}

@test "pytest with INTERNALERROR is passed through (tee on failure)" {
    local text
    text=$(printf '%s\n' \
        '============================= test session starts ==============================' \
        'INTERNALERROR> Traceback (most recent call last):' \
        'INTERNALERROR>   File "_pytest/main.py", line 270, in wrap_session' \
        'INTERNALERROR>     session.exitstatus = doit(config, session) or 0' \
        'INTERNALERROR> RuntimeError: pytest internal failure' \
        '============================= 0 passed in 0.01s ==============================')
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "pytest -v"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    refute_output --partial "modifyOutput"
    assert_output --partial "suppressOutput"
}

@test "non-pytest command with PASSED-like line is unaffected" {
    # A `git log` line containing PASSED in commit message must not trigger.
    local text
    text=$(printf '%s\n' \
        'commit abc123' \
        'Author: Wendel' \
        '    feat: tests PASSED [100%]')
    local input
    input=$(jq -n --arg text "$text" '{
        tool_name: "Bash",
        tool_input: {command: "git log -1"},
        tool_response: {content: [{text: $text}]},
        session_id: "test-sess",
        transcript_path: "/home/testuser/.claude/projects/test/main.jsonl"
    }')
    run_hook "$HOOK" "$input"
    assert_success
    refute_output --partial "modifyOutput"
    assert_output --partial "suppressOutput"
}
