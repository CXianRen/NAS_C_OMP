#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bin_dir="$script_dir/bin"

benchmarks="BT LU CG SP MG FT"
mkdir -p "$bin_dir"

build() {
  local build_class="$1"
  make -C "$script_dir" -j BENCHMARKS="$benchmarks" CLASS="$build_class" \
    2>&1 | tee "$bin_dir/build_${build_class}.log"
}

build C
build D
build S
