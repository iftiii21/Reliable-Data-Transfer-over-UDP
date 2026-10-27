# Runnable Stop-and-Wait and Go-Back-N endpoints

`src/arq_endpoint.c` is a small network/file wrapper around the existing ARQ,
packet, UDP, and file-transfer modules. It does not reimplement the ARQ state
machines or alter packet serialization. The experiment adapters are
`experiments/adapters/sw.sh` and `experiments/adapters/gbn.sh`; both follow the
same receiver/sender argument contract as the SR adapter.

## Wire and sequence behavior

- DATA sequence numbers count packets, start at zero, and advance by one per
  packet. Payloads are read and written with the existing file-transfer API.
- ACK N means “the receiver expects sequence N next.” Stop-and-Wait accepts
  only its current sequence plus one. Go-Back-N accepts cumulative ACKs within
  its outstanding range. Duplicate, stale, future, or malformed ACKs do not
  advance sender state.
- The receiver writes a DATA packet only when its sequence equals the next
  expected sequence. Duplicate or out-of-order DATA receives the current
  cumulative ACK and is not written twice. Go-Back-N retransmits its entire
  outstanding range after a timeout.
- FIN consumes the next sequence number. The receiver acknowledges FIN with
  ACK `FIN sequence + 1`, flushes/closes the output, and remains available for
  2.75 seconds to answer retransmitted FIN packets if that ACK was lost.
- Sequence numbers are unsigned 32-bit packet counters and naturally wrap in
  the ARQ helpers. Since the shared file writer maps sequence numbers directly
  to file offsets, the endpoint refuses DATA after a full sequence-space wrap
  rather than overwriting the beginning of a file larger than 2^32 chunks.

## Timer, retry, and exit policy

Both modes use a fixed 250 ms retransmission timeout and allow up to eight
retransmissions per outstanding Stop-and-Wait DATA packet or Go-Back-N timeout
round. GBN retry-round accounting lives in the endpoint wrapper because the
existing GBN state helper retains packets but intentionally has no retry
counter. FIN also gets up to eight retries. Exhaustion, socket/file errors,
invalid command arguments, or inability to write metrics returns nonzero.
Successful senders return zero only after the matching FIN ACK and write
`retransmissions=INTEGER` to the requested metrics file. Receivers return zero
after a valid FIN and the bounded duplicate-FIN linger; a 120-second idle
receiver exits nonzero.

The GBN window is limited to 1..1024 packets to bound endpoint memory. The
experiment matrix uses window 1 for SW and its configured GBN windows for GBN.
The receiver argument includes the window for adapter compatibility but its
in-order delivery logic does not need that value.

## Build and test

Build the endpoint with the project's strict flags and the existing source
modules, then run:

```sh
bash tests/test_arq_emulator.sh
```

The bounded test checks clean, DATA-loss, and ACK-loss transfers for each mode.
It requires emulator event logs, retransmission metrics for both loss cases,
successful sender/receiver exit statuses, and matching SHA-256 hashes. This
validates the two endpoint adapters and their controlled loss recovery; the
full three-protocol experiment still requires running the actual matrix.
