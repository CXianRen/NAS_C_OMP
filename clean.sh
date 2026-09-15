#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

components=(
  NPB3.3-OMP-C
  example
  framework/hams
  framework/j2025
  framework/region_control
  framework/timer
)

for component in "${components[@]}"; do
  echo "Cleaning $component"
  make -C "$script_dir/$component" clean
done
