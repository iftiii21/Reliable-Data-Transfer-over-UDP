#include "file_transfer.h"

#include <stdio.h>
#include <string.h>

#define TEST_SIZE 3000
#define MAX_TEST_PACKETS 10

int main(void)
{
    const char *input_filename = "/tmp/file_test_input.bin";
    const char *output_filename = "/tmp/file_test_output.bin";

    /*
     * ------------------------------------------------------------
     * Step 1: Create a known 3000-byte test file.
     * ------------------------------------------------------------
     */
    FILE *input = fopen(input_filename, "wb");

    if (input == NULL)
    {
        perror("fopen input");
        return 1;
    }

    for (int i = 0; i < TEST_SIZE; i++)
    {
        unsigned char value = (unsigned char)(i % 256);

        if (fwrite(&value, 1, 1, input) != 1)
        {
            perror("fwrite input");
            fclose(input);
            return 1;
        }
    }

    fclose(input);

    /*
     * ------------------------------------------------------------
     * Step 2: Read the file through our file segmentation API.
     * ------------------------------------------------------------
     */
    FileSender sender;

    if (file_sender_open(&sender, input_filename) != 0)
    {
        printf("Failed to open sender file\n");
        return 1;
    }

    Packet packets[MAX_TEST_PACKETS];
    int packet_count = 0;

    while (packet_count < MAX_TEST_PACKETS)
    {
        int result = file_sender_next_packet(
            &sender,
            &packets[packet_count]
        );

        if (result == -1)
        {
            printf("Error while reading file\n");
            file_sender_close(&sender);
            return 1;
        }

        if (result == 0)
        {
            break;
        }

        printf(
            "Created packet %u with %u bytes\n",
            packets[packet_count].header.sequence_number,
            packets[packet_count].header.payload_length
        );

        packet_count++;
    }

    file_sender_close(&sender);

    printf("Total packets: %d\n", packet_count);

    /*
     * A 3000-byte file should produce:
     * Packet 0 -> 1024 bytes
     * Packet 1 -> 1024 bytes
     * Packet 2 -> 952 bytes
     */
    if (packet_count != 3)
    {
        printf("Unexpected packet count\n");
        return 1;
    }

    /*
     * ------------------------------------------------------------
     * Step 3: Reassemble in OUT-OF-ORDER sequence.
     *
     * We intentionally write:
     *
     *     Packet 2
     *     Packet 0
     *     Packet 1
     *
     * The receiver should still reconstruct the correct file.
     * ------------------------------------------------------------
     */
    FileReceiver receiver;

    if (file_receiver_open(&receiver, output_filename) != 0)
    {
        printf("Failed to open receiver file\n");
        return 1;
    }

    if (file_receiver_write_packet(&receiver, &packets[2]) != 0)
    {
        printf("Failed to write packet 2\n");
        file_receiver_close(&receiver);
        return 1;
    }

    if (file_receiver_write_packet(&receiver, &packets[0]) != 0)
    {
        printf("Failed to write packet 0\n");
        file_receiver_close(&receiver);
        return 1;
    }

    if (file_receiver_write_packet(&receiver, &packets[1]) != 0)
    {
        printf("Failed to write packet 1\n");
        file_receiver_close(&receiver);
        return 1;
    }

    file_receiver_close(&receiver);

    printf("Out-of-order reassembly completed.\n");

    /*
     * ------------------------------------------------------------
     * Step 4: Compare original and reconstructed files.
     * ------------------------------------------------------------
     */
    FILE *original = fopen(input_filename, "rb");
    FILE *reconstructed = fopen(output_filename, "rb");

    if (original == NULL || reconstructed == NULL)
    {
        printf("Failed to open files for comparison\n");

        if (original != NULL)
        {
            fclose(original);
        }

        if (reconstructed != NULL)
        {
            fclose(reconstructed);
        }

        return 1;
    }

    int different = 0;

    for (int i = 0; i < TEST_SIZE; i++)
    {
        int original_byte = fgetc(original);
        int reconstructed_byte = fgetc(reconstructed);

        if (original_byte != reconstructed_byte)
        {
            different = 1;
            break;
        }
    }

    /*
     * Make sure the reconstructed file does not contain
     * extra data after the expected 3000 bytes.
     */
    if (!different && fgetc(reconstructed) != EOF)
    {
        different = 1;
    }

    fclose(original);
    fclose(reconstructed);

    if (different)
    {
        printf("FILE TEST FAILED\n");
        return 1;
    }

    printf("FILE TEST PASSED\n");

    return 0;
}