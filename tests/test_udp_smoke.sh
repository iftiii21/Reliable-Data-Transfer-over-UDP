#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
receiver_pid=

cleanup() {
    if [ -n "$receiver_pid" ]; then
        kill "$receiver_pid" 2>/dev/null || true
        wait "$receiver_pid" 2>/dev/null || true
    fi
    rm -rf "$tmpdir"
}
trap cleanup EXIT HUP INT TERM

timeout --kill-after=2s 10s ./build/udp_receiver_test >"$tmpdir/receiver.log" 2>&1 &
receiver_pid=$!
sleep 0.2
timeout --kill-after=2s 10s ./build/udp_sender_test >"$tmpdir/sender.log" 2>&1
wait "$receiver_pid"
receiver_pid=

grep -F "Sent 32 bytes" "$tmpdir/sender.log" >/dev/null
grep -F "Packet checksum VALID" "$tmpdir/receiver.log" >/dev/null
grep -F "Payload: Hello from Utkarsh!" "$tmpdir/receiver.log" >/dev/null
cat "$tmpdir/receiver.log"
printf '%s\n' "UDP smoke test passed"
