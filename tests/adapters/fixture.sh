#!/usr/bin/env bash
# Plumbing validation only: output is reconstructed at the echo client.
# Never label this fixture as Stop-and-Wait, GBN, or Selective Repeat results.
set -euo pipefail
role=$1
shift
case "$role" in
    receiver) exec ./build/udp_fixture echo "$1";;
    sender) exec ./build/udp_fixture roundtrip "$2" "$3" "$RDT_OUTPUT" "$5";;
    *) exit 2;;
esac
