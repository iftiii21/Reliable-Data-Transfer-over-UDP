#!/usr/bin/env bash
# Bounded SW/GBN integration tests through the byte-opaque channel relay.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
for tool in timeout sha256sum awk grep; do command -v "$tool" >/dev/null; done
[[ -x build/arq_endpoint && -x build/channel_proxy ]]

workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT
input="$root/docs/validation/fixture-input.bin"
base=$((20000 + ($$ % 17000) * 2))

run_case() {
    local protocol=$1 impairment=$2 direction=$3 loss=$4 window=$5 port=$6
    local matrix="$workspace/one.csv" output="$workspace/${protocol}_${impairment}"
    printf 'protocol,loss,reorder,window,seed\n%s,%s,0,%s,2026\n' \
        "$protocol" "$loss" "$window" >"$matrix"
    if ! RUN_TIMEOUT=35 BASE_PORT="$port" DIRECTION="$direction" \
        DELAY_MS=10 JITTER_MS=0 REORDER_MS=25 DUPLICATE=0 CORRUPT=0 \
        timeout --kill-after=2 45 bash experiments/run_matrix.sh \
        "$matrix" "$input" "$output" experiments/adapters; then
        for log in "$output"/run0001/*.log; do
            [[ ! -f "$log" ]] || { echo "--- $log" >&2; tail -n 30 "$log" >&2; }
        done
        for evidence in "$output"/run0001/metrics.txt "$output"/run0001/elapsed.txt; do
            [[ ! -f "$evidence" ]] || { echo "--- $evidence" >&2; cat "$evidence" >&2; }
        done
        return 1
    fi
    awk -F, 'NR==2 {if ($19!="ok" || $17!=$18 || $14<=0) exit 1; n++} END{if(n!=1)exit 1}' \
        "$output/runs.csv"
    grep -q '^SUMMARY' "$output/run0001/proxy.log"
    if [[ "$impairment" == data_loss ]]; then
        grep -q ',data,.*loss,' "$output/run0001/channel.csv"
    elif [[ "$impairment" == ack_loss ]]; then
        grep -q ',ack,.*loss,' "$output/run0001/channel.csv"
    fi
    if [[ "$impairment" == data_loss ||
          ( "$impairment" == ack_loss && "$protocol" == sw ) ]]; then
        awk -F= '$1=="retransmissions" {if($2<1)exit 1; found=1} END{exit !found}' \
            "$output/run0001/metrics.txt"
    fi
    echo "$protocol $impairment passed; SHA-256 $(sha256sum "$output/run0001/received.bin" | awk '{print $1}')"
}

run_case sw clean both 0 1 "$base"
run_case sw data_loss data 0.15 1 "$((base+2))"
run_case sw ack_loss ack 0.35 1 "$((base+4))"
run_case gbn clean both 0 8 "$((base+6))"
run_case gbn data_loss data 0.15 8 "$((base+8))"
run_case gbn ack_loss ack 0.35 8 "$((base+10))"
