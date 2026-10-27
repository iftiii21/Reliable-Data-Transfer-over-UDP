#!/usr/bin/env bash
# Adapter for the shared Go-Back-N endpoint.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
mode=gbn
role=$1
shift
binary=${ARQ_BIN:-"$root/build/arq_endpoint"}
case "$role" in
    receiver)
        [[ $# == 3 ]]
        exec "$binary" "$mode" receiver "$@"
        ;;
    sender)
        [[ $# == 5 ]]
        host=$1 port=$2 input=$3 window=$4 metrics=$5
        "$binary" "$mode" sender "$host" "$port" "$input" "$window" "$metrics"
        grep -Eq '^retransmissions=[0-9]+$' "$metrics"
        ;;
    *) echo "Expected sender or receiver role" >&2; exit 2;;
esac
