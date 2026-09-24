#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 NODE [hot|all]" >&2
  echo "  hot: 3 normal + 3 reverse-order control/native runs" >&2
  echo "  all: hot tests plus 30-process cold checks (default)" >&2
  exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage
node=$1
test_set=${2:-all}
[[ $test_set == hot || $test_set == all ]] || usage

artifact_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$artifact_dir/.." && pwd)
build_dir="$project_dir/framework/ut/build/control_api_overhead"
benchmark="$build_dir/control_api_overhead_benchmark"
module_name=${PHAMS_CLANG_MODULE:-Clang/18.1.8-GCCcore-13.3.0}
runs=${RUNS:-3}
cold_samples=${COLD_SAMPLES:-30}

if command -v ml >/dev/null 2>&1; then
  ml load "$module_name"
elif command -v module >/dev/null 2>&1; then
  module load "$module_name"
else
  echo "cannot find the environment-modules command" >&2
  exit 1
fi

mkdir -p "$build_dir"

clang -O3 -std=c11 -fopenmp -UNDEBUG \
  -I"$project_dir/framework/region_control" \
  -c "$project_dir/framework/region_control/region_control.c" \
  -o "$build_dir/region_control.o"

clang++ -O3 -std=c++17 -fopenmp -UNDEBUG \
  -I"$project_dir/framework/hams" \
  -I"$project_dir/framework/region_control" \
  -I"$project_dir/framework/tuner" \
  "$artifact_dir/control_api_overhead_benchmark.cpp" \
  "$project_dir/framework/hams/hams_binding.cpp" \
  "$project_dir/framework/tuner/tuner.cpp" \
  "$project_dir/framework/dummy/dummy.cpp" \
  "$project_dir/framework/offline/offline.cpp" \
  "$project_dir/framework/j2025/j2025.cpp" \
  "$project_dir/framework/j2025_b/j2025_b.cpp" \
  "$project_dir/framework/otter/otter.cpp" \
  "$build_dir/region_control.o" \
  $(pkg-config --cflags --libs hwloc 2>/dev/null || echo -lhwloc) \
  -o "$benchmark"

remote_setup="ml load '$module_name' >/dev/null 2>&1 && \
unset OMP_PLACES KMP_AFFINITY GOMP_CPU_AFFINITY && \
export OMP_NUM_THREADS=64 OMP_THREAD_LIMIT=64 OMP_DYNAMIC=false \
OMP_PROC_BIND=false REGION_TIME_REPORT=0"

run_hot() {
  local order=$1
  local run=$2
  local argument=
  [[ $order == reverse ]] && argument=reverse
  echo "order=$order run=$run"
  ssh -o BatchMode=yes "$node" \
    "$remote_setup && '$benchmark' compare $argument" \
    | grep -E '^(native|control)_'
}

echo "node=$node module=$module_name git=$(git -C "$project_dir" rev-parse --short=12 HEAD)"
for ((run = 1; run <= runs; ++run)); do
  run_hot normal "$run"
done
for ((run = 1; run <= runs; ++run)); do
  run_hot reverse "$run"
done

[[ $test_set == all ]] || exit 0

summarize_cold() {
  local implementation=$1
  local label=$2
  local from_threads=$3
  local from_layout=$4
  local to_threads=$5
  local to_layout=$6

  ssh -o BatchMode=yes "$node" \
    "$remote_setup && sample=0; while [ \"\$sample\" -lt '$cold_samples' ]; do \
       '$benchmark' single '$implementation' '$from_threads' '$from_layout' \
       '$to_threads' '$to_layout' '$label'; sample=\$((sample + 1)); done" \
    | awk -v label="$label" '$1 == label { print $2 }' \
    | sort -n \
    | awk -v label="$label" '
        { value[NR] = $1; sum += $1 }
        END {
          if (!NR) exit 1
          if (NR % 2) median = value[(NR + 1) / 2]
          else median = (value[NR / 2] + value[NR / 2 + 1]) / 2
          p90 = value[int(0.90 * (NR - 1)) + 1]
          p99 = value[int(0.99 * (NR - 1)) + 1]
          printf "%-26s n=%d mean_us=%.3f min_us=%.3f p50_us=%.3f p90_us=%.3f p99_us=%.3f max_us=%.3f\n", \
                 label, NR, sum / NR, value[1], median, p90, p99, value[NR]
        }'
}

for implementation in native hams; do
  summarize_cold "$implementation" "${implementation}_first_64C" 0 close 64 close
  summarize_cold "$implementation" "${implementation}_first_32C" 0 close 32 close
  summarize_cold "$implementation" "${implementation}_first_32S" 0 close 32 spread
  summarize_cold "$implementation" "${implementation}_64C_to_32C" 64 close 32 close
  summarize_cold "$implementation" "${implementation}_32C_to_64C" 32 close 64 close
  summarize_cold "$implementation" "${implementation}_1C_to_64C" 1 close 64 close
  summarize_cold "$implementation" "${implementation}_32C_to_32S" 32 close 32 spread
  summarize_cold "$implementation" "${implementation}_32S_to_32C" 32 spread 32 close
done
