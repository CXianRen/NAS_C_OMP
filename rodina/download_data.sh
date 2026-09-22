#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p data/cfd data/hotspot

base_url="https://raw.githubusercontent.com/msasongko17/rodinia_data/862ada1b99b106f9cb4af4a86c13dc0a615f9a5f"

curl -fL --retry 3 "$base_url/cfd/fvcorr.domn.097K" -o data/cfd/fvcorr.domn.097K
curl -fL --retry 3 "$base_url/cfd/fvcorr.domn.193K" -o data/cfd/fvcorr.domn.193K
curl -fL --retry 3 "$base_url/hotspot/temp_1024" -o data/hotspot/temp_1024
curl -fL --retry 3 "$base_url/hotspot/power_1024" -o data/hotspot/power_1024
