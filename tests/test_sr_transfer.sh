#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
receiver_pid=
port=$((20000 + ($$ % 40000)))

cleanup() {
    status=$?
    if [ -n "$receiver_pid" ]; then
        kill "$receiver_pid" 2>/dev/null || true
        wait "$receiver_pid" 2>/dev/null || true
    fi
    if [ "$status" -ne 0 ]; then
        [ ! -f "$tmpdir/sender.log" ] || cat "$tmpdir/sender.log"
        [ ! -f "$tmpdir/receiver.log" ] || cat "$tmpdir/receiver.log"
    fi
    rm -rf "$tmpdir"
}
trap cleanup EXIT HUP INT TERM

dd if=/dev/urandom of="$tmpdir/input.bin" bs=3073 count=1 status=none
timeout --kill-after=2s 20s ./build/sr_receiver "$port" "$tmpdir/output.bin" 8 \
    >"$tmpdir/receiver.log" 2>&1 &
receiver_pid=$!
sleep 0.2
timeout --kill-after=2s 25s ./build/sr_sender 127.0.0.1 "$port" "$tmpdir/input.bin" 8 \
    >"$tmpdir/sender.log" 2>&1
wait "$receiver_pid"
receiver_pid=

cmp "$tmpdir/input.bin" "$tmpdir/output.bin"
grep -F "Transfer finished" "$tmpdir/sender.log" >/dev/null
grep -F "Receive complete" "$tmpdir/receiver.log" >/dev/null
printf '%s\n' "Selective Repeat localhost file-transfer test passed"
