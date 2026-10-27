#!/usr/bin/env bash
# Bounded protocol-level checks through the channel emulator. These are SR
# integration checks, not comparative SW/GBN/SR performance measurements.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
for tool in timeout sha256sum awk grep; do command -v "$tool" >/dev/null; done
[[ -x build/sr_sender && -x build/sr_receiver && -x build/channel_proxy ]]

workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT
matrix="$workspace/sr.csv"
printf 'protocol,loss,reorder,window,seed\nsr,0.15,0,8,2026\n' >"$matrix"
export SR_BIN_DIR="$root/build"
input="$root/docs/validation/fixture-input.bin"
base=$((30000 + ($$ % 12000) * 2))

run_case() {
    local mode=$1 direction=$2 loss=$3 duplicate=$4 corrupt=$5 delay=$6 reorder=$7
    local out="$workspace/$mode" port
    printf 'protocol,loss,reorder,window,seed\nsr,%s,%s,8,2026\n' "$loss" "$reorder" >"$matrix"
    # Port offsets are distinct across sequential runs; each run has a hard cap.
    case "$mode" in
        data_loss) port=$base;;
        ack_loss) port=$((base+2));;
        corruption) port=$((base+4));;
        duplication) port=$((base+6));;
        reordering) port=$((base+8));;
        delay) port=$((base+10));;
    esac
    if ! RUN_TIMEOUT=35 BASE_PORT="$port" DIRECTION="$direction" DELAY_MS="$delay" \
        JITTER_MS=0 REORDER_MS=100 DUPLICATE="$duplicate" CORRUPT="$corrupt" \
        timeout --kill-after=2 45 bash experiments/run_matrix.sh \
        "$matrix" "$input" "$out" experiments/adapters; then
        for log in "$out"/run0001/*.log; do
            [[ ! -f "$log" ]] || { echo "--- $log" >&2; tail -n 30 "$log" >&2; }
        done
        return 1
    fi
    awk -F, 'NR==2 {if ($19!="ok" || $17!=$18 || $14<=0) exit 1; n++} END {if(n!=1) exit 1}' "$out/runs.csv"
    grep -q '^SUMMARY' "$out/run0001/proxy.log"
    case "$mode" in
        data_loss) grep -q ',data,.*loss,' "$out/run0001/channel.csv"
                   awk -F= '$1=="retransmissions"{if($2<1)exit 1;found=1} END{exit !found}' "$out/run0001/metrics.txt";;
        ack_loss) grep -q ',ack,.*loss,' "$out/run0001/channel.csv"
                  awk -F= '$1=="retransmissions"{if($2<1)exit 1;found=1} END{exit !found}' "$out/run0001/metrics.txt";;
        corruption) awk -F, '$4=="enqueue" && $9==1 {found=1} END{exit !found}' "$out/run0001/channel.csv";;
        duplication) grep -q ',enqueue,1,' "$out/run0001/channel.csv";;
        reordering) awk -F, '$4=="enqueue" && $8==1 {found=1} END{exit !found}' "$out/run0001/channel.csv";;
        delay) awk -F, '$4=="enqueue" && $7-$1>=40000 {ok=1} END{exit !ok}' "$out/run0001/channel.csv";;
    esac
    echo "SR emulator $mode passed; SHA-256 $(sha256sum "$out/run0001/received.bin" | awk '{print $1}')"
}

run_case data_loss data 0.15 0 0 10 0
run_case ack_loss ack 0.15 0 0 10 0
run_case corruption both 0 0 0.2 10 0
run_case duplication both 0 0.4 0 10 0
run_case reordering both 0 0 0 10 0.6
run_case delay both 0 0 0 40 0
