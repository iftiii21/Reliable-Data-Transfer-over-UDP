#!/usr/bin/env bash
# POSIX tool orchestration only; all network traffic/emulation runs in C.
set -euo pipefail
export LC_ALL=C
if [[ $# != 4 ]]; then
    echo "Usage: bash experiments/run_matrix.sh MATRIX.csv INPUT OUTPUT_DIR ADAPTER_DIR" >&2
    exit 2
fi
matrix=$(realpath "$1")
source_file=$(realpath "$2")
output=$(realpath -m "$3")
adapters=$(realpath "$4")
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
for tool in timeout sha256sum awk grep wc; do command -v "$tool" >/dev/null; done
[[ -x /usr/bin/time && -f "$matrix" && -f "$source_file" ]]
[[ -f build/channel_proxy || -f build/channel_proxy.exe ]]
[[ "$output" != *','* && "$output" != *$'\n'* ]]
delay=${DELAY_MS:-10}
jitter=${JITTER_MS:-0}
hold=${REORDER_MS:-25}
duplicate=${DUPLICATE:-0}
corrupt=${CORRUPT:-0}
direction=${DIRECTION:-both}
run_timeout=${RUN_TIMEOUT:-30}
base=${BASE_PORT:-29000}
[[ "$base" =~ ^[0-9]+$ && "$run_timeout" =~ ^[0-9]+$ ]]
((base>0 && base<65535 && run_timeout>0 && run_timeout<=3500))
mkdir -p "$(dirname "$output")"
mkdir "$output" # Refuse to overwrite previous experimental evidence.
bytes=$(wc -c < "$source_file" | tr -d '[:space:]')
source_hash=$(sha256sum "$source_file" | cut -d' ' -f1)
cp "$matrix" "$output/matrix.csv"
{
    echo "commit=$(git rev-parse HEAD)"
    echo "working_tree_dirty=$(git status --porcelain | wc -l)"
    echo "utc=$(date -u +%FT%TZ)"
    echo "platform=$(uname -a)"
    echo "input=$source_file"
    echo "source_sha256=$source_hash"
    echo "delay_ms=$delay jitter_ms=$jitter reorder_ms=$hold duplicate=$duplicate corrupt=$corrupt direction=$direction"
    echo "timeout_s=$run_timeout base_port=$base"
    echo "elapsed=time -f %e (sender process, includes CLI startup; 10ms resolution)"
    for adapter in "$adapters"/*.sh; do
        [[ ! -f "$adapter" ]] || sha256sum "$adapter"
    done
} > "$output/metadata.txt"
csv="$output/runs.csv"
echo 'run_id,protocol,loss,reorder,window,seed,delay_ms,jitter_ms,reorder_ms,duplicate,corrupt,direction,bytes,elapsed_s,retransmissions,goodput_bps,sha256_source,sha256_received,status' > "$csv"
receiver_pid= proxy_pid=
cleanup() {
    if [[ -n "$proxy_pid" ]]; then kill "$proxy_pid" 2>/dev/null || true; wait "$proxy_pid" 2>/dev/null || true; proxy_pid=; fi
    if [[ -n "$receiver_pid" ]]; then kill "$receiver_pid" 2>/dev/null || true; wait "$receiver_pid" 2>/dev/null || true; receiver_pid=; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
failures=0
index=0
while IFS=, read -r protocol loss reorder window seed extra; do
    seed=${seed%$'\r'}
    [[ "$protocol" != protocol && -n "$protocol" ]] || continue
    [[ "$protocol" =~ ^[a-z][a-z0-9_-]*$ && "$window" =~ ^[0-9]+$ && "$seed" =~ ^[0-9]+$ && -z "$extra" ]]
    awk -v l="$loss" -v r="$reorder" 'BEGIN {exit !(l ~ /^[0-9]+([.][0-9]+)?$/ && r ~ /^[0-9]+([.][0-9]+)?$/ && l>=0 && l<=1 && r>=0 && r<=1)}'
    ((window>0 && window<=65535))
    index=$((index+1))
    id=$(printf 'run%04d' "$index")
    dir="$output/$id"
    mkdir "$dir"
    adapter="$adapters/$protocol.sh"
    elapsed= retransmissions= received_hash= goodput=
    status=ok
    if [[ ! -f "$adapter" ]]; then
        status=missing_adapter
    else
        export RDT_OUTPUT="$dir/received.bin"
        timeout --kill-after=2 "$run_timeout" bash "$adapter" receiver "$((base+1))" "$RDT_OUTPUT" "$window" >"$dir/receiver.log" 2>&1 &
        receiver_pid=$!
        # Adapters must bind before printing READY; receiver startup is excluded.
        receiver_ready=0
        for ((attempt=0; attempt<100; attempt++)); do
            if grep -q '^READY' "$dir/receiver.log"; then receiver_ready=1; break; fi
            kill -0 "$receiver_pid" 2>/dev/null || break
            sleep 0.02
        done
        if (( ! receiver_ready )); then status=receiver_start_failed; fi
        if [[ "$status" == ok ]]; then
            ./build/channel_proxy --listen-port "$base" --receiver-port "$((base+1))" --seed "$seed" \
                --loss "$loss" --reorder "$reorder" --delay-ms "$delay" --jitter-ms "$jitter" \
                --reorder-ms "$hold" --duplicate "$duplicate" --corrupt "$corrupt" --direction "$direction" \
                --run-ms "$((run_timeout*1000+3000))" --idle-ms "$((run_timeout*1000+2000))" \
                --stop-file "$dir/stop" --log "$dir/channel.csv" 2>"$dir/proxy.log" &
            proxy_pid=$!
            proxy_ready=0
            for ((attempt=0; attempt<100; attempt++)); do
                if grep -q '^READY' "$dir/proxy.log"; then proxy_ready=1; break; fi
                kill -0 "$proxy_pid" 2>/dev/null || break
                sleep 0.02
            done
            if (( ! proxy_ready )); then status=proxy_start_failed; fi
        fi
        if [[ "$status" == ok ]]; then
            if /usr/bin/time -f '%e' -o "$dir/elapsed.txt" timeout --kill-after=2 "$run_timeout" \
                bash "$adapter" sender 127.0.0.1 "$base" "$source_file" "$window" "$dir/metrics.txt" \
                >"$dir/sender.log" 2>&1; then
                if wait "$receiver_pid"; then receiver_pid=; else receiver_pid=; status=receiver_failed; fi
            else
                status=sender_failed_or_timeout
            fi
        fi
        if [[ "$status" == ok ]]; then
            : > "$dir/stop"
            if wait "$proxy_pid"; then proxy_pid=; else proxy_pid=; status=proxy_failed; fi
        fi
        cleanup
        if [[ -f "$dir/received.bin" ]]; then received_hash=$(sha256sum "$dir/received.bin" | cut -d' ' -f1); fi
        if [[ "$status" == ok && "$received_hash" != "$source_hash" ]]; then status=hash_mismatch; fi
        if [[ "$status" == ok ]]; then
            elapsed=$(tail -n1 "$dir/elapsed.txt")
            retransmissions=$(awk -F= '$1=="retransmissions"{print $2}' "$dir/metrics.txt" 2>/dev/null || true)
            if [[ ! "$retransmissions" =~ ^[0-9]+$ ]] ||
                ! awk -v t="$elapsed" 'BEGIN {exit !(t ~ /^[0-9]+([.][0-9]+)?$/ && t>0)}'; then
                status=invalid_metrics
            elif grep -Eq ',(overflow|send_error|shutdown_drop),' "$dir/channel.csv"; then
                status=channel_incomplete
            else
                goodput=$(awk -v b="$bytes" -v t="$elapsed" 'BEGIN {printf "%.6f", b*8/t}')
            fi
        fi
    fi
    if [[ "$status" != ok ]]; then failures=$((failures+1)); goodput=; fi
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$id" "$protocol" "$loss" "$reorder" "$window" "$seed" "$delay" "$jitter" "$hold" \
        "$duplicate" "$corrupt" "$direction" "$bytes" "$elapsed" "$retransmissions" "$goodput" \
        "$source_hash" "$received_hash" "$status" >> "$csv"
    echo "$id $protocol loss=$loss reorder=$reorder window=$window seed=$seed status=$status"
done < "$matrix"
((index>0 && failures==0))
