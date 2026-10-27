#!/usr/bin/env bash
# Adapter for origin/indraneel at 8a7fa8e. Set SR_BIN_DIR to its built directory.
set -euo pipefail
role=$1
shift
: "${SR_BIN_DIR:?Set SR_BIN_DIR to the directory containing sr_sender and sr_receiver}"
case "$role" in
    receiver)
        # Existing SR CLI prints only after initialization; stdbuf makes READY
        # visible even while stdout is redirected. The adapter translates its
        # post-bind message to the runner's READY contract.
        stdbuf -oL "$SR_BIN_DIR/sr_receiver" "$@" |
            awk '/^\[SR Receiver\] Listening/{print "READY sr"; fflush()} {print; fflush()}'
        ;;
    sender)
        [[ $# == 5 ]]
        host=$1 port=$2 input=$3 window=$4 metrics=$5
        logfile="$metrics.sender"
        "$SR_BIN_DIR/sr_sender" "$host" "$port" "$input" "$window" >"$logfile" 2>&1
        cat "$logfile"
        # Never infer success from the current CLI's exit status alone.
        grep -q 'FIN ACK received. Transfer complete.' "$logfile"
        count=$(awk '/Retransmissions[[:space:]]*:/ {print $3}' "$logfile")
        [[ "$count" =~ ^[0-9]+$ ]]
        fin_count=$(awk '/FIN retransmit #[0-9]+/{n++} END{print n+0}' "$logfile")
        printf 'retransmissions=%s\n' "$((count+fin_count))" >"$metrics"
        ;;
    *) exit 2;;
esac
