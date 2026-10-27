# ARQ integration guide

This guide describes the public sender-side APIs in `include/arq.h`. They
operate on host-order `Packet` and `PacketHeader` structures from
`include/packet.h`. They do not open sockets, serialize packets, calculate or
verify checksums, run timers, or transmit data.

## Stop-and-Wait API

### `stop_wait_init(state, first_sequence, retry_limit)`

Initializes a `StopWaitState` at `first_sequence`. `retry_limit` is the maximum
number of retransmissions after the initial transmission; zero disables
retries. A null `state` is ignored because this function returns `void`.

### `stop_wait_make_data(state, packet, data, length)`

Builds a host-order `PACKET_DATA` packet and retains a copy in `state` as the
one outstanding packet. `length` must not exceed `MAX_PAYLOAD_SIZE`; `data`
must be non-null when `length` is nonzero. A zero-length payload may use a null
data pointer. The caller owns `packet` and may pass the constructed packet to
its serialization/send layer.

Returns `0` on success and `-1` for null arguments, invalid payload input, or
when another packet is already outstanding. The sequence number advances only
after the matching cumulative ACK is accepted.

### `stop_wait_on_ack(state, ack)`

Consumes a host-order ACK header. Returns `1` when the ACK advances state,
`0` for a duplicate, stale, future, or otherwise non-advancing ACK number, and
`-1` for a null state/header or a header whose type is not `PACKET_ACK`.

The required ACK number is `state->sequence_number + 1` with unsigned 32-bit
wraparound. Before calling this function, the transport must validate the
datagram length, deserialize it, and validate magic, protocol version, and
checksum. The function does not validate those fields.

### `stop_wait_on_timeout(state, retransmission)`

Copies the retained packet into caller-owned `retransmission`. Returns `1` when
one retry copy is produced, `0` when there is no outstanding packet or the
retry limit is exhausted, and `-1` for a null argument. Each successful call
increments `retry_count`. It does not transmit the packet or schedule another
timer.

## Go-Back-N API

### `go_back_n_init(state, first_sequence, window_size, packet_storage, packet_capacity)`

Initializes GBN state and clears `window_size` entries in the caller-owned
`packet_storage` array. `window_size` must be nonzero, storage must be
non-null, and `packet_capacity` (in `Packet` entries) must be at least the
window size. The storage array must remain alive and writable for the lifetime
of the state. Returns `0` on success and `-1` for invalid arguments or
insufficient capacity.

### `go_back_n_can_send(state)`

Returns nonzero if initialized state has room in the send window; otherwise
returns zero. A successful send consumes one window slot until cumulative ACK
advancement.

### `go_back_n_make_data(state, packet, data, length)`

Builds a host-order `PACKET_DATA` packet, sets its sequence number and the
configured window size, retains a full copy in the next caller-owned storage
slot, and advances `next_sequence`. Payload pointer and length rules match
`stop_wait_make_data`.

Returns `0` on success and `-1` for invalid state/arguments/payload or when
the send window is full. The caller owns the output `packet`; it must not point
at a currently outstanding entry in the retention array. Retained packets
remain owned by the caller-provided storage.

### `go_back_n_on_ack(state, ack)`

Consumes a host-order cumulative ACK whose number is the next sequence expected
by the receiver. Returns the number of newly acknowledged packets, `0` for a
duplicate, stale, or future ACK number, and `-1` for null state/header, missing
storage, or a non-ACK packet type. Advancing ACKs slide the retained packet
array so slot zero remains `base`. The transport must validate datagram length,
magic, version, and checksum before calling this function.

### `go_back_n_on_timeout(state, retransmissions, capacity)`

Copies every outstanding retained packet, from `base` through
`next_sequence - 1`, in order, into a caller-owned output array. It returns the
number of packets that need retry. If `retransmissions` is null or `capacity`
is too small, it returns the required count without copying anything or
changing sender state. The output array should be separate from packet storage.
This function prepares copies only; the caller sends them and manages timers.

## Validation and ownership contract

The ARQ routines perform local pointer, payload length, window capacity, and
packet type checks described above. ACK handlers rely on the transport to
validate received datagram framing/length, packet magic, protocol version,
checksum, and byte order before constructing/passing a `PacketHeader`.
Outgoing packet structures are host-order values; serialization and checksum
calculation happen after packet construction. Caller-owned buffers must have
the declared capacity and stay alive while referenced by state.

## Decisions to confirm with the team

The repository does not yet define these integration interfaces. Confirm them
with the transport/team before connecting the modules:

1. **ACK meaning:** this implementation assumes cumulative ACK numbers name
   the next sequence expected. If the agreed protocol ACKs the last received
   packet or uses byte offsets, the ACK logic/sequence progression must change.
2. **Sequence unit and start:** data sequence numbers advance by one per
   packet, independent of payload length, and the caller chooses the initial
   value. Confirm this matches the wire protocol and sender/receiver startup.
3. **Window field meaning:** GBN writes its configured sender window into the
   existing 16-bit `window_size` header field. Confirm that field represents
   this value rather than an advertised receive window or another quantity.
4. **Wire encoding:** define field byte order, exact serialized header size,
   padding policy, and whether payload bytes follow the header directly. Do
   not serialize the C struct by raw memory copy unless the team explicitly
   defines that ABI-dependent format.
5. **Checksum:** agree the checksum algorithm, covered bytes, treatment of the
   checksum field while calculating it, and validation order. The ARQ module
   neither calculates nor validates checksums.
6. **Transport API and timer ownership:** choose how constructed packets reach
   UDP send, how ACKs are delivered to the handlers, which timer starts/resets
   on send/ACK, and how timeout callbacks invoke the timeout-copy functions.
7. **Retry policy:** choose a retry limit and exhaustion behavior for
   Stop-and-Wait. GBN currently has no retry counter/limit; its caller decides
   when and how often to retry the full outstanding range.
8. **Buffer lifetime and scheduling:** ensure the GBN packet array remains
   allocated for the state lifetime, and provide timeout output capacity for
   up to the configured window size. Decide whether retries are sent
   immediately in returned order or scheduled by the transport.

No packet format change is made or required by this guide. Receiver-side ARQ
ownership is not established by the available requirements. UDP transport,
serialization, checksum implementation, timers, and channel emulation remain
outside these ARQ APIs.
