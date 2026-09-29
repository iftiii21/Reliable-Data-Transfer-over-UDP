#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "packet.h"
#include <stdio.h>
#include <stdint.h>

/*
 * File sender state.
 *
 * The sender reads the input file in chunks of
 * MAX_PAYLOAD_SIZE bytes and converts each chunk
 * into a DATA packet.
 */
typedef struct
{
    FILE *file;
    uint32_t next_sequence_number;
} FileSender;


/*
 * File receiver state.
 *
 * Packets are written to their correct position
 * using the packet sequence number.
 *
 * This also allows out-of-order packets, which is
 * required later for Selective Repeat.
 */
typedef struct
{
    FILE *file;
} FileReceiver;


/*
 * Open a file for sending.
 *
 * Returns:
 *   0  -> success
 *  -1  -> failure
 */
int file_sender_open(
    FileSender *sender,
    const char *filename
);


/*
 * Read the next chunk of the file and create
 * a DATA packet.
 *
 * Returns:
 *   1  -> a packet was created
 *   0  -> end of file
 *  -1  -> error
 */
int file_sender_next_packet(
    FileSender *sender,
    Packet *packet
);


/*
 * Close the sender file.
 */
void file_sender_close(
    FileSender *sender
);


/*
 * Open/create a file for receiving.
 *
 * Returns:
 *   0  -> success
 *  -1  -> failure
 */
int file_receiver_open(
    FileReceiver *receiver,
    const char *filename
);
int file_receiver_write_packet(
    FileReceiver *receiver,
    const Packet *packet
);


/*
 * Close the receiver file.
 */
void file_receiver_close(
    FileReceiver *receiver
);

#endif