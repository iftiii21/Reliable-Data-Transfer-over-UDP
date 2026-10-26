/**
 * selective_repeat.c — Full Selective Repeat ARQ implementation
 *
 * Protocol summary:
 *
 * SENDER
 * ------
 * 1. Pre-fill the window from the file.
 * 2. Send all un-sent packets within [base, base+window_size).
 * 3. Poll for ACKs with select() (timeout = SR_POLL_US).
 * 4. On ACK receipt:
 *      a. Mark the slot acked.
 *      b. If it's the base, advance base past all consecutive acked slots
 *         and load new packets from file.
 *      c. If not a retransmit, feed elapsed time to RTT estimator.
 * 5. Scan all in-use slots; retransmit any whose timer has expired:
 *      a. Call rtt_backoff().
 *      b. Re-arm timer with new RTO.
 *      c. Set is_retransmit = 1, increment retransmit_count.
 * 6. When all data is ACKed (base == next_seq and window empty):
 *      a. Send FIN.
 *      b. Wait for FIN ACK with its own timer; retransmit FIN on timeout.
 *
 * RECEIVER
 * --------
 * 1. Receive packets with select() timeout.
 * 2. DATA in window  → buffer, send individual ACK.
 * 3. DATA below base → duplicate; resend ACK.
 * 4. Deliver all consecutive in-order buffered packets to file.
 * 5. FIN             → flush buffered data, send FIN ACK, exit.
 */

#define _POSIX_C_SOURCE 200112L  /* for clock_gettime, select */

#include "selective_repeat.h"
#include "udp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* POSIX / socket headers */
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>

/* =========================================================================
 * Internal helpers
 * ====================================================================== */

/**
 * seq_in_window - Test if a sequence number `seq` is within the receive
 * window [base, base + window_size).
 *
 * Works for uint32_t wrap-around arithmetic.
 */
static int seq_in_window(uint32_t seq, uint32_t base, uint32_t window_size) {
    return (uint32_t)(seq - base) < window_size;
}

/**
 * wait_for_packet - Use select() to wait for a readable packet on sockfd.
 *
 * @param sockfd    Socket to watch.
 * @param timeout_us  Microseconds to wait (0 = immediate poll).
 * @return 1 if data available, 0 if timeout, -1 on error.
 */
static int wait_for_packet(int sockfd, long timeout_us) {
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);

    tv.tv_sec  = timeout_us / 1000000L;
    tv.tv_usec = timeout_us % 1000000L;

    int ret = select(sockfd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        if (errno == EINTR) return 0; /* interrupted by signal, treat as timeout */
        perror("select");
        return -1;
    }
    return (ret > 0 && FD_ISSET(sockfd, &rfds)) ? 1 : 0;
}

/**
 * send_ack - Build and send an individual ACK for the given sequence number.
 */
static void send_ack(int sockfd,
                     const struct sockaddr_in *dest,
                     uint32_t seq_num) {
    Packet ack;
    create_ack_packet(&ack, seq_num);
    if (udp_send_packet(sockfd, &ack, dest) < 0) {
        fprintf(stderr, "[RX] WARNING: failed to send ACK %u\n", seq_num);
    }
}

/* =========================================================================
 * Sender — initialisation & cleanup
 * ====================================================================== */

int sr_sender_init(SRSender *sr,
                   int sockfd,
                   struct sockaddr_in peer,
                   uint32_t window_size,
                   const char *filename) {
    if (!sr || !filename) return -1;
    if (window_size == 0 || window_size > SR_MAX_WINDOW) {
        fprintf(stderr, "[TX] Invalid window_size %u (max %d)\n",
                window_size, SR_MAX_WINDOW);
        return -1;
    }

    memset(sr, 0, sizeof(*sr));

    sr->sockfd      = sockfd;
    sr->peer_addr   = peer;
    sr->window_size = window_size;
    sr->base        = 0;
    sr->next_seq    = 0;
    sr->transfer_complete = 0;

    /* Initialise RTT estimator and stats */
    rtt_init(&sr->rtt);
    stats_init(&sr->stats);

    /* Initialise all sender window slots */
    for (int i = 0; i < SR_MAX_WINDOW; i++) {
        memset(&sr->window[i], 0, sizeof(sr->window[i]));
        timer_init(&sr->window[i].timer);
        sr->window[i].in_use = 0;
        sr->window[i].acked  = 0;
    }

    /* Open the source file */
    if (file_sender_open(&sr->file_sender, filename) != 0) {
        fprintf(stderr, "[TX] Cannot open file: %s\n", filename);
        return -1;
    }

    printf("[TX] Sender initialised — file: %s, window: %u\n",
           filename, window_size);
    return 0;
}

void sr_sender_cleanup(SRSender *sr) {
    if (!sr) return;
    file_sender_close(&sr->file_sender);
}

/* =========================================================================
 * Sender — window management helpers
 * ====================================================================== */

/**
 * load_packet_into_window - Read the next chunk from the file and place it
 * into the window slot at index (seq % SR_MAX_WINDOW).
 *
 * @return 1 if a packet was loaded, 0 if EOF, -1 on error.
 */
static int load_packet_into_window(SRSender *sr) {
    /* Is the window full? */
    if ((uint32_t)(sr->next_seq - sr->base) >= sr->window_size) return 0;

    int idx = (int)(sr->next_seq % SR_MAX_WINDOW);
    SRSenderSlot *slot = &sr->window[idx];

    if (slot->in_use) {
        /* Slot already has an unacknowledged packet — window is full */
        return 0;
    }

    int result = file_sender_next_packet(&sr->file_sender, &slot->packet);
    if (result == 1) {
        /* Packet loaded; the sequence number is set by file_sender_next_packet
         * but we enforce our own sequence counter for safety. */
        slot->packet.header.sequence_number = sr->next_seq;
        prepare_packet(&slot->packet); /* recalculate checksum */

        slot->in_use         = 1;
        slot->acked          = 0;
        slot->retransmit_count = 0;
        slot->is_retransmit  = 0;
        timer_init(&slot->timer); /* armed when actually sent */

        sr->next_seq++;
        return 1;
    }
    return result; /* 0 = EOF, -1 = error */
}

/**
 * advance_base - Slide the sender window base past all consecutive acked
 * slots, clearing them so they can be reused.
 */
static void advance_base(SRSender *sr) {
    while (sr->base != sr->next_seq) {
        int idx = (int)(sr->base % SR_MAX_WINDOW);
        SRSenderSlot *slot = &sr->window[idx];

        if (!slot->in_use || !slot->acked) break;

        /* Clear the slot */
        timer_disarm(&slot->timer);
        slot->in_use = 0;
        slot->acked  = 0;
        sr->base++;
    }
}

/* =========================================================================
 * Sender — main run loop
 * ====================================================================== */

void sr_sender_run(SRSender *sr) {
    if (!sr) return;

    printf("[TX] Starting transfer...\n");

    /* Phase 1: pre-fill the window with packets from file */
    {
        int loaded;
        do {
            loaded = load_packet_into_window(sr);
        } while (loaded == 1);
    }

    int eof_reached   = 0; /* no more packets to load from file */
    int all_data_acked = 0;

    while (!sr->transfer_complete) {

        /* ----------------------------------------------------------------
         * Step 1: Send all un-sent (in_use but no timer armed) packets
         *         within the window.
         * -------------------------------------------------------------- */
        for (uint32_t seq = sr->base;
             seq != sr->next_seq &&
             (uint32_t)(seq - sr->base) < sr->window_size;
             seq++) {

            int idx = (int)(seq % SR_MAX_WINDOW);
            SRSenderSlot *slot = &sr->window[idx];

            if (!slot->in_use || slot->acked || slot->timer.armed) continue;

            /* First send — arm the timer and send */
            long rto = rtt_get_rto_us(&sr->rtt);
            if (udp_send_packet(sr->sockfd, &slot->packet,
                                &sr->peer_addr) > 0) {
                timer_arm(&slot->timer, rto);
                stats_record_send(&sr->stats,
                                  slot->packet.header.payload_length);
                printf("[TX] Sent seq=%u  (RTO=%ldms)\n",
                       seq, rto / 1000);
            } else {
                fprintf(stderr, "[TX] WARNING: send failed for seq=%u\n", seq);
            }
        }

        /* ----------------------------------------------------------------
         * Step 2: Poll for incoming ACKs (select with SR_POLL_US timeout).
         * -------------------------------------------------------------- */
        int avail = wait_for_packet(sr->sockfd, SR_POLL_US);
        if (avail > 0) {
            Packet ack_pkt;
            struct sockaddr_in sender;

            while (wait_for_packet(sr->sockfd, 0) > 0) {
                if (udp_receive_packet(sr->sockfd, &ack_pkt, &sender) < 0) break;
                if (!verify_packet(&ack_pkt)) continue;

                if (ack_pkt.header.type == PACKET_ACK) {
                    uint32_t ack_seq = ack_pkt.header.acknowledgement_number;

                    /* Is this ACK for a packet in our window? */
                    if (!seq_in_window(ack_seq, sr->base, sr->window_size))
                        continue;

                    int idx = (int)(ack_seq % SR_MAX_WINDOW);
                    SRSenderSlot *slot = &sr->window[idx];

                    if (slot->in_use && !slot->acked &&
                        slot->packet.header.sequence_number == ack_seq) {

                        /* Karn's: only update RTT if this was NOT a retransmit */
                        if (!slot->is_retransmit && slot->timer.armed) {
                            long sample_us = timer_elapsed_us(&slot->timer);
                            if (sample_us > 0) {
                                rtt_update(&sr->rtt, sample_us);
                            }
                        }

                        slot->acked = 1;
                        timer_disarm(&slot->timer);

                        printf("[TX] ACK  seq=%u  (SRTT=%ldms RTO=%ldms)\n",
                               ack_seq,
                               sr->rtt.srtt_us / 1000,
                               rtt_get_rto_us(&sr->rtt) / 1000);
                    }
                }
            }

            /* Advance base and load new packets */
            advance_base(sr);

            if (!eof_reached) {
                int loaded;
                do {
                    loaded = load_packet_into_window(sr);
                    if (loaded == 0) { eof_reached = 1; break; }
                } while (loaded == 1);
            }
        }

        /* ----------------------------------------------------------------
         * Step 3: Check timers — retransmit expired packets.
         * -------------------------------------------------------------- */
        for (uint32_t seq = sr->base;
             seq != sr->next_seq &&
             (uint32_t)(seq - sr->base) < sr->window_size;
             seq++) {

            int idx = (int)(seq % SR_MAX_WINDOW);
            SRSenderSlot *slot = &sr->window[idx];

            if (!slot->in_use || slot->acked) continue;

            if (timer_expired(&slot->timer)) {
                slot->retransmit_count++;

                if (slot->retransmit_count > SR_MAX_RETRANS) {
                    fprintf(stderr,
                        "[TX] ERROR: seq=%u exceeded max retransmits (%d). "
                        "Aborting.\n", seq, SR_MAX_RETRANS);
                    sr->transfer_complete = 1;
                    return;
                }

                /* Backoff RTO before retransmitting (Karn) */
                rtt_backoff(&sr->rtt);
                long new_rto = rtt_get_rto_us(&sr->rtt);

                slot->is_retransmit = 1;

                if (udp_send_packet(sr->sockfd, &slot->packet,
                                    &sr->peer_addr) > 0) {
                    timer_arm(&slot->timer, new_rto);
                    stats_record_retransmit(&sr->stats,
                                            slot->packet.header.payload_length);
                    printf("[TX] RETX  seq=%u  attempt=%d  (RTO=%ldms)\n",
                           seq, slot->retransmit_count, new_rto / 1000);
                } else {
                    /* Re-arm anyway so we try again */
                    timer_arm(&slot->timer, new_rto);
                }
            }
        }

        /* ----------------------------------------------------------------
         * Step 4: Check if all data has been ACKed.
         * -------------------------------------------------------------- */
        if (eof_reached && sr->base == sr->next_seq) {
            all_data_acked = 1;
        }

        /* ----------------------------------------------------------------
         * Step 5: FIN handshake — send FIN and wait for FIN ACK.
         * -------------------------------------------------------------- */
        if (all_data_acked && !sr->transfer_complete) {
            printf("[TX] All data ACKed. Sending FIN (seq=%u)...\n",
                   sr->next_seq);

            Packet fin_pkt;
            create_fin_packet(&fin_pkt, sr->next_seq);

            RetransTimer fin_timer;
            timer_init(&fin_timer);

            int fin_retrans = 0;
            int fin_done    = 0;

            while (!fin_done && fin_retrans <= SR_MAX_FIN_RETRANS) {
                if (udp_send_packet(sr->sockfd, &fin_pkt,
                                    &sr->peer_addr) > 0) {
                    timer_arm(&fin_timer, SR_FIN_TIMEOUT_US);
                    if (fin_retrans > 0)
                        printf("[TX] FIN retransmit #%d\n", fin_retrans);
                } else {
                    fprintf(stderr, "[TX] WARNING: FIN send failed\n");
                }

                /* Wait for FIN ACK */
                while (!timer_expired(&fin_timer)) {
                    long remaining = SR_FIN_TIMEOUT_US -
                                     timer_elapsed_us(&fin_timer);
                    if (remaining <= 0) break;

                    int avl = wait_for_packet(sr->sockfd,
                                              remaining < SR_POLL_US
                                                  ? remaining : SR_POLL_US);
                    if (avl <= 0) continue;

                    Packet resp;
                    struct sockaddr_in from;
                    if (udp_receive_packet(sr->sockfd, &resp, &from) < 0)
                        continue;
                    if (!verify_packet(&resp)) continue;

                    if (resp.header.type == PACKET_ACK &&
                        resp.header.acknowledgement_number ==
                            fin_pkt.header.sequence_number) {
                        printf("[TX] FIN ACK received. Transfer complete.\n");
                        fin_done = 1;
                        break;
                    }
                }

                if (!fin_done) {
                    fin_retrans++;
                    if (fin_retrans > SR_MAX_FIN_RETRANS) {
                        printf("[TX] FIN max retransmits reached. "
                               "Assuming receiver got it.\n");
                    }
                }
            }

            sr->transfer_complete = 1;
        }
    } /* while (!transfer_complete) */

    stats_finish(&sr->stats);
    printf("[TX] Transfer finished.\n");
    stats_print(&sr->stats);
}

/* =========================================================================
 * Receiver — initialisation & cleanup
 * ====================================================================== */

int sr_receiver_init(SRReceiver *sr,
                     int sockfd,
                     struct sockaddr_in peer,
                     uint32_t window_size,
                     const char *output_filename) {
    if (!sr || !output_filename) return -1;
    if (window_size == 0 || window_size > SR_MAX_WINDOW) {
        fprintf(stderr, "[RX] Invalid window_size %u (max %d)\n",
                window_size, SR_MAX_WINDOW);
        return -1;
    }

    memset(sr, 0, sizeof(*sr));

    sr->sockfd        = sockfd;
    sr->peer_addr     = peer;
    sr->window_size   = window_size;
    sr->base          = 0;
    sr->last_delivered = (uint32_t)-1; /* none yet */
    sr->fin_received  = 0;
    sr->fin_seq       = 0;

    /* Initialise buffer */
    for (int i = 0; i < SR_MAX_WINDOW; i++) {
        sr->buffer[i].received = 0;
    }

    /* Open output file */
    if (file_receiver_open(&sr->file_receiver, output_filename) != 0) {
        fprintf(stderr, "[RX] Cannot open output file: %s\n", output_filename);
        return -1;
    }

    printf("[RX] Receiver initialised — output: %s, window: %u\n",
           output_filename, window_size);
    return 0;
}

void sr_receiver_cleanup(SRReceiver *sr) {
    if (!sr) return;
    file_receiver_close(&sr->file_receiver);
}

/* =========================================================================
 * Receiver — deliver in-order buffered packets to file
 * ====================================================================== */

/**
 * deliver_in_order - Write all consecutive in-order packets from the buffer
 * to the file, starting from sr->base.
 *
 * After delivery, advances sr->base and clears delivered slots.
 */
static void deliver_in_order(SRReceiver *sr) {
    while (1) {
        int idx = (int)(sr->base % SR_MAX_WINDOW);
        SRReceiverSlot *slot = &sr->buffer[idx];

        if (!slot->received) break;
        if (slot->packet.header.sequence_number != sr->base) break;

        /* Write to file at the correct offset */
        if (file_receiver_write_packet(&sr->file_receiver,
                                       &slot->packet) < 0) {
            fprintf(stderr, "[RX] ERROR: failed to write seq=%u to file\n",
                    sr->base);
        } else {
            printf("[RX] Delivered seq=%u  (%u bytes)\n",
                   sr->base,
                   slot->packet.header.payload_length);
        }

        sr->last_delivered = sr->base;
        slot->received = 0;
        sr->base++;
    }
}

/* =========================================================================
 * Receiver — main run loop
 * ====================================================================== */

void sr_receiver_run(SRReceiver *sr) {
    if (!sr) return;

    printf("[RX] Waiting for packets...\n");

    while (!sr->fin_received) {
        /* Wait for incoming packet */
        int avail = wait_for_packet(sr->sockfd, SR_POLL_US * 10);
        if (avail <= 0) {
            /* Timeout — loop and check again */
            continue;
        }

        Packet pkt;
        struct sockaddr_in sender;

        if (udp_receive_packet(sr->sockfd, &pkt, &sender) < 0) continue;
        if (!verify_packet(&pkt)) {
            printf("[RX] Dropped corrupted packet\n");
            continue;
        }

        /* ----------------------------------------------------------------
         * Handle FIN
         * -------------------------------------------------------------- */
        if (pkt.header.type == PACKET_FIN) {
            uint32_t fin_seq = pkt.header.sequence_number;
            printf("[RX] FIN received (seq=%u)\n", fin_seq);

            /* Flush any remaining buffered data */
            deliver_in_order(sr);

            /* Send FIN ACK */
            Packet fin_ack;
            create_ack_packet(&fin_ack, fin_seq);
            if (udp_send_packet(sr->sockfd, &fin_ack, &sender) < 0) {
                /* Try again once */
                udp_send_packet(sr->sockfd, &fin_ack, &sender);
            }
            printf("[RX] FIN ACK sent. Receive loop complete.\n");

            sr->fin_received = 1;
            sr->fin_seq      = fin_seq;
            break;
        }

        /* ----------------------------------------------------------------
         * Handle DATA
         * -------------------------------------------------------------- */
        if (pkt.header.type == PACKET_DATA) {
            uint32_t seq = pkt.header.sequence_number;

            /* Remember sender address (for first packet or re-validation) */
            sr->peer_addr = sender;

            if (seq_in_window(seq, sr->base, sr->window_size)) {
                /* Packet is within the receiver window */
                int idx = (int)(seq % SR_MAX_WINDOW);
                SRReceiverSlot *slot = &sr->buffer[idx];

                if (!slot->received) {
                    /* Store the packet in the buffer */
                    slot->packet   = pkt;
                    slot->received = 1;
                    printf("[RX] Buffered seq=%u\n", seq);
                } else {
                    /* Already have this packet — just re-ACK */
                    printf("[RX] Duplicate (buffered) seq=%u — re-ACKing\n", seq);
                }

                /* Send individual ACK for this packet */
                send_ack(sr->sockfd, &sender, seq);

                /* Deliver any newly in-order packets */
                deliver_in_order(sr);

            } else if ((uint32_t)(sr->base - seq - 1) < sr->window_size) {
                /*
                 * Packet is below the window (already received & delivered).
                 * Re-send ACK so the sender can advance its window.
                 */
                printf("[RX] Old duplicate seq=%u (base=%u) — re-ACKing\n",
                       seq, sr->base);
                send_ack(sr->sockfd, &sender, seq);
            } else {
                /* Packet is completely outside both windows — discard */
                printf("[RX] Out-of-range seq=%u (base=%u) — discarding\n",
                       seq, sr->base);
            }
        }
    }

    printf("[RX] Receive complete (last delivered seq=%u).\n",
           sr->last_delivered);
}
