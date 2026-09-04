#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

components=(
  NPB3.3-OMP-C
  LULESH
  otter
)

for component in "${components[@]}"; do
  echo "Cleaning $component"
  make -C "$script_dir/$component" clean
done
