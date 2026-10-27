#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
workspace=$(mktemp -d)
input=build/channel_proxy
[[ ! -f build/channel_proxy.exe ]] || input=build/channel_proxy.exe
dd if="$input" of="$workspace/input.bin" bs=1024 count=12 2>/dev/null
bash experiments/run_matrix.sh tests/fixture_matrix.csv "$workspace/input.bin" "$workspace/success" tests/adapters
awk -F, 'NR>1 {if ($19!="ok" || $17!=$18 || $14<=0) exit 1; n++} END {if(n!=9) exit 1}' "$workspace/success/runs.csv"
for log in "$workspace"/success/run*/proxy.log; do grep -q '^SUMMARY' "$log"; done
if bash experiments/run_matrix.sh tests/missing_adapter_matrix.csv "$workspace/input.bin" "$workspace/missing" tests/adapters; then
    echo "Missing adapters must fail" >&2; exit 1
fi
grep -q ',missing_adapter$' "$workspace/missing/runs.csv"
before=$(sha256sum "$workspace/success/runs.csv")
if bash experiments/run_matrix.sh tests/fixture_matrix.csv "$workspace/input.bin" "$workspace/success" tests/adapters 2>/dev/null; then
    echo "Existing evidence directory must not be overwritten" >&2; exit 1
fi
[[ "$before" == "$(sha256sum "$workspace/success/runs.csv")" ]]
echo "Experiment runner tests passed; evidence: $workspace"
