#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${SG_BUILD_DIR:-$repo_root/build/native}"

command -v git >/dev/null 2>&1 || { echo "git is required" >&2; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "cmake is required" >&2; exit 1; }

if command -v nproc >/dev/null 2>&1; then
  jobs="$(nproc)"
else
  jobs="2"
fi

cd "$repo_root"
git submodule update --init --recursive
cmake -S . -B "$build_dir" -DSG_BUILD_NATIVE=ON
cmake --build "$build_dir" -j"$jobs"
"$build_dir/native/asg-install" "$@"
