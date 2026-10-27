# Stop-and-Wait and Go-Back-N design

The repository has no written team requirements or transport implementation.
`include/packet.h` is the shared packet contract and remains unchanged. These
modules populate `PACKET_DATA` packet structures and consume `PACKET_ACK`
headers in host byte order. They implement sender state and retained-packet
copies; they do not implement receiver-side ARQ, UDP I/O, or packet
transmission.

## Protocol assumptions to confirm with the team

- ACK `acknowledgement_number` is cumulative and names the next sequence
  number expected. Therefore an ACK of `N` acknowledges data through `N-1`.
- Data packet sequence numbers advance by one packet, independent of payload
  length. Sequence arithmetic uses unsigned 32-bit wraparound.
- A sender starts at a caller-selected sequence. Stop-and-Wait permits one
  outstanding packet. GBN uses a caller-selected nonzero window, stored in the
  existing 16-bit `window_size` field.
- A GBN timeout returns copies of every outstanding packet from `base` through
  `next_sequence - 1`, in sequence order, including across sequence wrap.
- Stop-and-Wait retry limit counts retransmissions after the original send. A
  limit of zero disables retries. Timeout returns a copy of the retained
  packet until the limit is reached; it does not send it.

These assumptions avoid a shared packet format change. If the team instead
uses ACKs for the last received sequence or byte offsets, the ACK handling and
sequence advancement need to be adapted before integration.

## Implemented behavior

- Stop-and-Wait allows one outstanding packet, retains its complete `Packet`,
  advances only on the expected cumulative ACK, and provides timeout copies
  bounded by a configurable retry limit.
- Go-Back-N enforces its configured window, retains each outstanding complete
  `Packet` in caller-provided storage of at least `window_size` entries, slides
  that storage on cumulative ACK advancement, and copies the entire outstanding
  range for a timeout retry.
- ACK handlers reject null state/header and a non-ACK packet type. Duplicate
  and out-of-range ACK numbers do not advance state.
- Packet construction checks the payload bound and requires non-NULL data for
  nonempty payloads. Sequence arithmetic is unsigned 32-bit arithmetic.
- GBN rejects an output packet pointer that aliases a currently outstanding
  retention slot, preserving the packet needed for a later timeout retry.

## Caller and transport responsibilities

Before an ACK reaches an ACK handler, the caller must validate the received
datagram length, deserialize fields into host byte order, and validate packet
magic, protocol version, and checksum. The handler itself checks pointer
arguments and `PACKET_ACK` type, but does not repeat magic/version/checksum
validation. The sender must serialize outgoing packet fields, calculate their
checksum, manage UDP sockets and timers, and send the packet copies returned by
timeout handling. The caller must also arrange an appropriate retry limit and
decide what to do when Stop-and-Wait reports exhausted retries. No network
transmission, receiver behavior, or channel emulation is implemented or tested.

The GBN packet buffer is caller-owned: its lifetime must cover the state
machine, and its capacity passed to `go_back_n_init` must be at least the
window size. Timeout output requires a separate caller-owned array with room
for the returned count; if it is too small, the function reports the required
count and copies nothing.
