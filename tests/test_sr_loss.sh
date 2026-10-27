#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
receiver_pid=
proxy_pid=
base_port=$((20000 + ($$ % 30000)))

cleanup() {
    status=$?
    if [ -n "$proxy_pid" ]; then
        kill "$proxy_pid" 2>/dev/null || true
        wait "$proxy_pid" 2>/dev/null || true
    fi
    if [ -n "$receiver_pid" ]; then
        kill "$receiver_pid" 2>/dev/null || true
        wait "$receiver_pid" 2>/dev/null || true
    fi
    if [ "$status" -ne 0 ]; then
        for log in "$tmpdir"/*.log; do
            [ ! -f "$log" ] || { printf '%s\n' "--- $log"; cat "$log"; }
        done
    fi
    rm -rf "$tmpdir"
}
trap cleanup EXIT HUP INT TERM

run_case() {
    loss_kind=$1
    loss_seq=$2
    port_offset=$3
    receiver_port=$((base_port + port_offset))
    proxy_port=$((receiver_port + 1))
    input="$tmpdir/${loss_kind}_input.bin"
    output="$tmpdir/${loss_kind}_output.bin"
    receiver_log="$tmpdir/${loss_kind}_receiver.log"
    proxy_log="$tmpdir/${loss_kind}_proxy.log"
    sender_log="$tmpdir/${loss_kind}_sender.log"

    dd if=/dev/urandom of="$input" bs=16381 count=1 status=none
    timeout --kill-after=2s 20s ./build/sr_receiver "$receiver_port" "$output" 8 \
        >"$receiver_log" 2>&1 &
    receiver_pid=$!
    timeout --kill-after=2s 15s python3 tests/udp_loss_proxy.py \
        --listen-port "$proxy_port" --receiver-port "$receiver_port" \
        --drop "$loss_kind" --sequence "$loss_seq" >"$proxy_log" 2>&1 &
    proxy_pid=$!
    sleep 0.2

    timeout --kill-after=2s 30s ./build/sr_sender 127.0.0.1 "$proxy_port" "$input" 8 \
        >"$sender_log" 2>&1

    kill "$proxy_pid" 2>/dev/null || true
    wait "$proxy_pid" 2>/dev/null || true
    proxy_pid=

    wait "$receiver_pid"
    receiver_pid=

    cmp "$input" "$output"
    if [ "$loss_kind" = data ]; then
        grep -F "DROPPED_DATA seq=$loss_seq" "$proxy_log" >/dev/null
        grep -F "FORWARDED_DATA_RETRY seq=$loss_seq" "$proxy_log" >/dev/null
        grep -F "RETX  seq=$loss_seq" "$sender_log" >/dev/null
    else
        grep -F "DROPPED_ACK seq=$loss_seq" "$proxy_log" >/dev/null
        grep -F "FORWARDED_ACK_RETRY seq=$loss_seq" "$proxy_log" >/dev/null
        grep -F "RETX  seq=$loss_seq" "$sender_log" >/dev/null
    fi
}

run_case data 1 0
run_case ack 2 2
printf '%s\n' "Selective Repeat DATA-loss and ACK-loss tests passed"
