/**
 * selective_repeat.h — Selective Repeat ARQ over UDP
 *
 * Implements the Selective Repeat sliding-window protocol:
 *   - Sender: maintains a window of unacknowledged packets, retransmits
 *     individual timed-out packets (not the whole window).
 *   - Receiver: buffers out-of-order packets, sends individual ACKs,
 *     and delivers in-order data to the file layer.
 *
 * Key design decisions:
 *   - Window size is limited to SR_MAX_WINDOW (64).  For correctness,
 *     the actual window_size passed at init must be <= SR_MAX_WINDOW / 2
 *     (i.e., <= 32) when sequence numbers can wrap, but we allow up to 64
 *     here since uint32_t sequence numbers effectively never wrap.
 *   - ACKs are individual (not cumulative): each DATA packet received
 *     is acknowledged with an ACK carrying that packet's sequence number.
 *   - RTT estimation uses Jacobson/Karels (rtt.h) with Karn's algorithm.
 *   - Timers use clock_gettime(CLOCK_MONOTONIC) (timer.h).
 *   - I/O uses select() with short timeouts (never blocking recvfrom).
 */

#ifndef SELECTIVE_REPEAT_H
#define SELECTIVE_REPEAT_H

#include <stdint.h>
#include <netinet/in.h>

#include "packet.h"
#include "file_transfer.h"
#include "rtt.h"
#include "timer.h"
#include "stats.h"

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

/** Maximum window size (must be a power of two for clean modulo indexing). */
#define SR_MAX_WINDOW   64

/** select() poll timeout in microseconds (10 ms). */
#define SR_POLL_US      10000L

/** Maximum number of retransmissions before giving up on a packet. */
#define SR_MAX_RETRANS  20

/** Timeout (microseconds) to wait for a FIN-ACK after sending FIN. */
#define SR_FIN_TIMEOUT_US  2000000L   /* 2 s */

/** Maximum FIN retransmissions before declaring transfer done anyway. */
#define SR_MAX_FIN_RETRANS 5

/* -------------------------------------------------------------------------
 * Sender-side window slot
 * ---------------------------------------------------------------------- */

/**
 * SRSenderSlot — one slot in the sender's retransmit window.
 *
 * Fields:
 *   packet          — The DATA packet stored in this slot.
 *   timer           — Per-packet retransmission timer.
 *   in_use          — 1 if this slot holds a packet awaiting ACK.
 *   acked           — 1 if ACK received (but slot not yet slid past).
 *   retransmit_count — Number of times this packet has been retransmitted.
 *   is_retransmit   — 1 if the most recent send was a retransmit.
 *                     Used by Karn's algorithm to suppress RTT sampling.
 */
typedef struct {
    Packet       packet;
    RetransTimer timer;
    int          in_use;
    int          acked;
    int          retransmit_count;
    int          is_retransmit;
} SRSenderSlot;

/* -------------------------------------------------------------------------
 * Receiver-side buffer slot
 * ---------------------------------------------------------------------- */

/**
 * SRReceiverSlot — one slot in the receiver's reorder buffer.
 *
 * Fields:
 *   packet   — Buffered DATA packet.
 *   received — 1 if this slot holds a received (but not yet delivered) packet.
 */
typedef struct {
    Packet packet;
    int    received;
} SRReceiverSlot;

/* -------------------------------------------------------------------------
 * Sender struct
 * ---------------------------------------------------------------------- */

/**
 * SRSender — all state needed to run the selective-repeat sender.
 *
 * Fields:
 *   sockfd          — UDP socket file descriptor.
 *   peer_addr       — Receiver's address.
 *   base            — Sequence number of the oldest unacknowledged packet.
 *   next_seq        — Next sequence number to be loaded from file.
 *   window_size     — Number of packets the window can hold (≤ SR_MAX_WINDOW).
 *   window[]        — Array of sender slots indexed by seq % SR_MAX_WINDOW.
 *   rtt             — Adaptive RTT estimator.
 *   file_sender     — File reading state.
 *   stats           — Transfer statistics.
 *   transfer_complete — Set to 1 when all data is ACKed and FIN is done.
 */
typedef struct {
    int                sockfd;
    struct sockaddr_in peer_addr;
    uint32_t           base;
    uint32_t           next_seq;
    uint32_t           window_size;
    SRSenderSlot       window[SR_MAX_WINDOW];
    RttEstimator       rtt;
    FileSender         file_sender;
    TransferStats      stats;
    int                transfer_complete;
} SRSender;

/* -------------------------------------------------------------------------
 * Receiver struct
 * ---------------------------------------------------------------------- */

/**
 * SRReceiver — all state needed to run the selective-repeat receiver.
 *
 * Fields:
 *   sockfd         — UDP socket file descriptor.
 *   peer_addr      — Sender's address (learned from first packet).
 *   base           — Next expected in-order sequence number.
 *   window_size    — Receiver window size (≤ SR_MAX_WINDOW).
 *   buffer[]       — Reorder buffer indexed by seq % SR_MAX_WINDOW.
 *   file_receiver  — File writing state.
 *   last_delivered — Sequence number of the last packet written to file.
 *   fin_received   — 1 after FIN has been processed.
 *   fin_seq        — Sequence number of the FIN packet.
 */
typedef struct {
    int                sockfd;
    struct sockaddr_in peer_addr;
    uint32_t           base;
    uint32_t           window_size;
    SRReceiverSlot     buffer[SR_MAX_WINDOW];
    FileReceiver       file_receiver;
    uint32_t           last_delivered;
    int                fin_received;
    uint32_t           fin_seq;
} SRReceiver;

/* -------------------------------------------------------------------------
 * Sender API
 * ---------------------------------------------------------------------- */

/**
 * sr_sender_init - Initialise the sender and open the source file.
 *
 * @param sr          Sender state (caller-allocated).
 * @param sockfd      Connected/unconnected UDP socket.
 * @param peer        Receiver address.
 * @param window_size Sliding-window size (1 – SR_MAX_WINDOW).
 * @param filename    Path to the file to send.
 * @return 0 on success, -1 on error (file open failure, bad params).
 */
int sr_sender_init(SRSender *sr,
                   int sockfd,
                   struct sockaddr_in peer,
                   uint32_t window_size,
                   const char *filename);

/**
 * sr_sender_run - Execute the send loop until the transfer is complete.
 *
 * Runs until:
 *   1. All DATA packets are ACKed.
 *   2. FIN is sent and acknowledged (or max FIN retransmits exceeded).
 *
 * The function prints progress to stdout.
 */
void sr_sender_run(SRSender *sr);

/**
 * sr_sender_cleanup - Close the source file and release resources.
 */
void sr_sender_cleanup(SRSender *sr);

/* -------------------------------------------------------------------------
 * Receiver API
 * ---------------------------------------------------------------------- */

/**
 * sr_receiver_init - Initialise the receiver and open the output file.
 *
 * @param sr              Receiver state (caller-allocated).
 * @param sockfd          Bound UDP socket.
 * @param peer            Sender's address (used to validate incoming packets).
 * @param window_size     Receiver window size (1 – SR_MAX_WINDOW).
 * @param output_filename Path for the received file.
 * @return 0 on success, -1 on error.
 */
int sr_receiver_init(SRReceiver *sr,
                     int sockfd,
                     struct sockaddr_in peer,
                     uint32_t window_size,
                     const char *output_filename);

/**
 * sr_receiver_run - Execute the receive loop until FIN is processed.
 */
void sr_receiver_run(SRReceiver *sr);

/**
 * sr_receiver_cleanup - Close the output file and release resources.
 */
void sr_receiver_cleanup(SRReceiver *sr);

#endif /* SELECTIVE_REPEAT_H */
