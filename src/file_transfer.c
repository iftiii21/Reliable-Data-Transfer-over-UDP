#include "file_transfer.h"

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

/*
 * Open the input file for reading.
 */
int file_sender_open(
    FileSender *sender,
    const char *filename
)
{
    if (sender == NULL || filename == NULL)
    {
        return -1;
    }

    sender->file = fopen(filename, "rb");

    if (sender->file == NULL)
    {
        return -1;
    }

    sender->next_sequence_number = 0;

    return 0;
}


/*
 * Read the next chunk of the file and turn it into
 * a DATA packet.
 *
 * Returns:
 *   1  -> packet created
 *   0  -> end of file
 *  -1  -> error
 */
int file_sender_next_packet(
    FileSender *sender,
    Packet *packet
)
{
    size_t bytes_read;
    char buffer[MAX_PAYLOAD_SIZE];

    if (sender == NULL || sender->file == NULL || packet == NULL)
    {
        return -1;
    }

    bytes_read = fread(
        buffer,
        1,
        MAX_PAYLOAD_SIZE,
        sender->file
    );

    if (bytes_read == 0)
    {
        if (ferror(sender->file))
        {
            return -1;
        }

        return 0;
    }

    create_data_packet(
        packet,
        sender->next_sequence_number,
        buffer,
        (uint16_t)bytes_read
    );

    sender->next_sequence_number++;

    return 1;
}


/*
 * Close the sender's input file.
 */
void file_sender_close(
    FileSender *sender
)
{
    if (sender == NULL)
    {
        return;
    }

    if (sender->file != NULL)
    {
        fclose(sender->file);
        sender->file = NULL;
    }
}


/*
 * Create/open the output file for receiving data.
 */
int file_receiver_open(
    FileReceiver *receiver,
    const char *filename
)
{
    if (receiver == NULL || filename == NULL)
    {
        return -1;
    }

    receiver->file = fopen(filename, "wb+");

    if (receiver->file == NULL)
    {
        return -1;
    }

    return 0;
}


/*
 * Write a DATA packet to its correct position.
 *
 * Sequence number determines the file offset:
 *
 * sequence 0 -> offset 0
 * sequence 1 -> offset 1024
 * sequence 2 -> offset 2048
 * ...
 *
 * This allows packets to arrive out of order.
 */
int file_receiver_write_packet(
    FileReceiver *receiver,
    const Packet *packet
)
{
    uint64_t offset;

    if (receiver == NULL ||
        receiver->file == NULL ||
        packet == NULL)
    {
        return -1;
    }

    if (packet->header.type != PACKET_DATA)
    {
        return -1;
    }

    if (packet->header.payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    /*
     * Make sure the calculated offset can be
     * represented by fseek().
     */
    offset =
        (uint64_t)packet->header.sequence_number *
        MAX_PAYLOAD_SIZE;

    if (offset > (uint64_t)LONG_MAX)
    {
        return -1;
    }

    if (fseek(
            receiver->file,
            (long)offset,
            SEEK_SET) != 0)
    {
        return -1;
    }

    if (fwrite(
            packet->payload,
            1,
            packet->header.payload_length,
            receiver->file
        ) != packet->header.payload_length)
    {
        return -1;
    }

    if (fflush(receiver->file) != 0)
    {
        return -1;
    }

    return 0;
}


/*
 * Close the receiver's output file.
 */
void file_receiver_close(
    FileReceiver *receiver
)
{
    if (receiver == NULL)
    {
        return;
    }

    if (receiver->file != NULL)
    {
        fclose(receiver->file);
        receiver->file = NULL;
    }
}