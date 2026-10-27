# Channel emulator

Owner: Aayushmann Kumar Singh, 2405253.

## Topology and integration

    sender -> proxy front socket :9000 -> proxy back socket -> receiver :9001
    sender <- proxy front socket :9000 <- proxy back socket <- receiver :9001

The sender sees replies from the same address it sent to. The receiver sees the
proxy back socket as its peer and must reply to that source address. No packet
format, checksum, ACK numbering, segmentation, or ARQ state is interpreted here.
This avoids the incompatible cumulative/individual ACK semantics across branches.
Checksums are deliberately not repaired after bit corruption.

The front socket binds 127.0.0.1. The back socket also binds loopback, so the
receiver IP must be a reachable loopback IPv4 address for this release. A single
sender address is pinned from the first accepted datagram; subsequent foreign
senders and responses from outside the configured receiver endpoint are ignored.
An unauthenticated first packet can claim a session, so this is an isolated local
experiment tool, not a publicly exposed relay. Restart it between transfers.

## Impairment semantics

All probabilities are rounded to integer parts per million (0..1,000,000).
For each accepted datagram, in each enabled direction:

1. Drop it with probability loss; dropped packets receive no other decisions.
2. If it survives, duplicate it with probability duplicate, producing two copies.
3. Select an extra hold with probability reorder.
4. Select corruption with probability corrupt. For a nonempty datagram, flip one
   uniformly selected bit in one uniformly selected byte of the serialized data.
5. Schedule delay = max(0, base delay + uniform signed jitter), then add the
   reorder holding delay if selected.

Both copies share corruption and due time. Duplicates are adjacent when due
times tie; independently scheduling copies is not part of this model. Empty
datagrams can be dropped/duplicated/delayed but have no bits to corrupt.
Jitter is sampled in integer microseconds, inclusive at both ends. Delay inputs
use whole milliseconds, each bounded to one hour.

Reordering is implemented as finite additional holding time, not an unbounded
wait for a following packet. It can invert order only when another datagram can
overtake the held datagram. At reorder=1 with fixed delay and no jitter, every
datagram receives the same hold and no inversion is expected. Jitter itself can
also reorder traffic. Keep jitter=0 for isolated reordering sweeps.

Default direction is both; data and ACK traffic use independent RNG states.
--direction data and --direction ack disable all impairments in the other
direction. Loss applies to all datagrams, including startup/FIN/ACK messages.

## Determinism

The pure C engine uses SplitMix64 with rejection sampling rather than libc rand.
The DATA RNG starts at the configured 64-bit seed, including zero. ACK RNG starts
at seed XOR 0xd1b54a32d192ed03. Given identical per-direction datagram lengths and
arrival sequences, decisions are reproducible across systems. Adding ACK traffic
does not advance the DATA generator.

The kernel scheduler, UDP buffering, timers, and retransmission traffic can
change arrival sequences, so equal seeds do not promise bit-for-bit replay of
real transfer timestamps or identical loss positions across different protocols.
Use repeated seeds and retain all traces. This is deterministic decision
generation conditional on input traffic, not deterministic operating-system
scheduling.

## Scheduling and resource bounds

A min-heap orders queued copies by monotonic due time, then insertion order.
Each datagram owns its byte buffer until forwarding. The select loop checks due
packets before reading another datagram; it never sleeps once per received
packet or serializes the configured propagation delay. Idle timeouts do not
discard queued delayed packets. --run-ms is an optional absolute lifetime.

The queue is bounded to 4096 copies by default (1..65536 configurable); memory is
O(queue limit * maximum accepted datagram size), potentially about 256 MiB at
the default bound and maximum IPv4 UDP payload. A full queue drops a new copy
and records overflow, distinct from configured random loss. select monitors two
sockets and wakes at least every 20ms; this is a teaching emulator, not a
high-throughput network appliance. Normal process signals close sockets and
free queued buffers; pending copies are logged as shutdown_drop.
The runner uses --stop-file PATH instead of platform-dependent termination:
when the marker appears, the proxy drains its scheduled copies and exits
normally, closing its event log. Create that marker only after sender and
receiver completion. A preexisting marker stops a new proxy immediately.

## CSV events

Columns: time_us,direction,packet_id,event,copy,length,due_us,reordered,corrupted,
corrupt_byte,corrupt_mask.

Times are relative to process start and monotonic. packet_id increments per
direction for each accepted datagram, not per ARQ sequence number. copy is 0
for the original and 1 for its duplicate. Enqueue rows carry impairment
decisions; forward rows carry the actual dispatch time and matching due time.
Join by direction,packet_id,copy to inspect scheduling latency.

Events are loss, enqueue, forward, overflow, send_error, shutdown_drop. The
stderr SUMMARY records directional received/forwarded/random-loss/overflow
counters. Forward counts include duplicates; received counts do not.
Log writes are checked when closing; disk failures result in nonzero exit.
SIGKILL/forced termination cannot flush buffered logs.
