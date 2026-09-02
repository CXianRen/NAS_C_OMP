#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

benchmarks="BT LU CG SP MG FT"
mkdir -p bin

build() {
  local build_class="$1"
  make -j BENCHMARKS="$benchmarks" CLASS="$build_class" \
    2>&1 | tee "bin/build_${build_class}.log"
}

build C
build D
build S
