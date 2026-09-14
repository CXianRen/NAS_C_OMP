#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

threads=${1:-4}
places=${2:-cores}
if [[ ! $threads =~ ^[1-9][0-9]*$ ]]; then
    echo "Usage: bash run.sh [positive thread count] [cores|threads|explicit places]" >&2
    exit 1
fi

make --no-print-directory binding_demo
for binding in false close spread primary; do
    printf '\n=== %s ===\n' "$binding"
    # Start a fresh runtime per policy, without vendor affinity overrides.
    env -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
        KMP_TOPOLOGY_METHOD=hwloc \
        OMP_DYNAMIC=false OMP_NUM_THREADS="$threads" \
        OMP_PLACES="$places" OMP_PROC_BIND="$binding" ./binding_demo
done
