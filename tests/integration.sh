#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)
base=$((20000 + ($$ % 14000) * 2))
receiver_pid= proxy_pid=
cleanup() {
    [[ -z "$proxy_pid" ]] || kill "$proxy_pid" 2>/dev/null || true
    [[ -z "$receiver_pid" ]] || kill "$receiver_pid" 2>/dev/null || true
}
trap cleanup EXIT

ready() {
    local logfile=$1 pid=$2
    for ((attempt=0; attempt<100; attempt++)); do
        grep -q '^READY' "$logfile" && return 0
        kill -0 "$pid" 2>/dev/null || { cat "$logfile"; return 1; }
        sleep 0.02
    done
    echo "Startup timed out: $logfile" >&2
    return 1
}

start() {
    local name=$1
    shift
    ./build/udp_fixture echo "$((base + 1))" 2>"$out/$name.receiver" &
    receiver_pid=$!
    ready "$out/$name.receiver" "$receiver_pid"
    ./build/channel_proxy --listen-port "$base" --receiver-port "$((base + 1))" \
        --seed 2026 --idle-ms 900 --log "$out/$name.csv" "$@" \
        2>"$out/$name.proxy" &
    proxy_pid=$!
    ready "$out/$name.proxy" "$proxy_pid"
}

finish() {
    wait "$receiver_pid"
    receiver_pid=
    wait "$proxy_pid"
    proxy_pid=
}

for mode in clean loss duplicate corrupt delay jitter reorder overflow; do
    options=(--direction data)
    case "$mode" in
        loss) options+=(--loss 1);;
        duplicate) options+=(--duplicate 1);;
        corrupt) options+=(--corrupt 1);;
        delay) options+=(--delay-ms 20);;
        jitter) options+=(--delay-ms 20 --jitter-ms 10);;
        reorder) options+=(--reorder 0.5 --reorder-ms 80);;
        overflow) options+=(--queue-limit 1 --delay-ms 100);;
    esac
    start "$mode" "${options[@]}"
    ./build/udp_fixture probe "$base" "$mode"
    finish
done

# ACK loss must be independent of data forwarding.
start ack-loss --direction ack --loss 1
./build/udp_fixture probe "$base" loss
finish
awk -F, '$2=="data" && $4=="forward"{data++} $2=="ack" && $4=="loss"{ack++}
    END {exit !(data==64 && ack==64)}' "$out/ack-loss.csv"

# Scheduled departures must never be early; constant and jitter delays differ.
awk -F, '$2=="data" && $4=="enqueue" {if ($7-$1!=20000) exit 1; n++}
    $4=="forward" && $1<$7 {exit 1} END {if(n!=64) exit 1}' "$out/delay.csv"
awk -F, '$2=="data" && $4=="enqueue" {d=$7-$1; if(d<10000 || d>30000) exit 1; values[d]=1}
    END {for(d in values) n++; if(n<2) exit 1}' "$out/jitter.csv"
grep -q ',overflow,' "$out/overflow.csv"

start boundaries
./build/udp_fixture boundaries "$base"
finish

# Real UDP roundtrip with all six impairments; SHA-256 checks useful bytes.
dd if=./build/channel_proxy of="$out/input.bin" bs=1024 count=12 2>/dev/null
start mixed --loss 0.12 --duplicate 0.2 --corrupt 0.08 --reorder 0.3 \
    --delay-ms 4 --jitter-ms 3 --reorder-ms 20
./build/udp_fixture roundtrip "$base" "$out/input.bin" "$out/output.bin" "$out/metrics"
finish
input_hash=$(sha256sum "$out/input.bin" | cut -d' ' -f1)
output_hash=$(sha256sum "$out/output.bin" | cut -d' ' -f1)
[[ "$input_hash" == "$output_hash" ]]
[[ $(awk -F= '$1=="retransmissions"{print $2}' "$out/metrics") -gt 0 ]]
cat "$out/metrics"
echo "SHA-256 verified: $input_hash"

for bad in nan -0.1 1.1 nope; do
    if ./build/channel_proxy --listen-port "$base" --receiver-port "$((base+1))" --loss "$bad" 2>/dev/null; then
        echo "Accepted invalid probability: $bad" >&2; exit 1
    fi
done
for bad in 0 -1 65536 1x; do
    if ./build/channel_proxy --listen-port "$bad" --receiver-port "$((base+1))" 2>/dev/null; then
        echo "Accepted invalid port: $bad" >&2; exit 1
    fi
done
echo "UDP integration tests passed; logs: $out"
