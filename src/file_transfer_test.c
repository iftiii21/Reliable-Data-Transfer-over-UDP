#include "file_transfer.h"

#include <stdio.h>
#include <string.h>

#define TEST_SIZE 3000
#define MAX_TEST_PACKETS 10

int main(void)
{
    const char *input_filename = "file_transfer_test_input.tmp";
    const char *output_filename = "file_transfer_test_output.tmp";
    FILE *input = NULL;
    FILE *original = NULL;
    FILE *reconstructed = NULL;
    FileSender sender = {0};
    FileReceiver receiver = {0};
    int input_created = 0;
    int output_created = 0;
    int result = 1;

    /*
     * ------------------------------------------------------------
     * Step 1: Create a known 3000-byte test file.
     * ------------------------------------------------------------
     */
    input = fopen(input_filename, "rb");
    if (input != NULL)
    {
        fclose(input);
        input = NULL;
        fprintf(stderr, "Temporary test input already exists: %s\n",
                input_filename);
        goto cleanup;
    }
    input = fopen(input_filename, "wb");

    if (input == NULL)
    {
        perror("fopen input");
        goto cleanup;
    }
    input_created = 1;

    for (int i = 0; i < TEST_SIZE; i++)
    {
        unsigned char value = (unsigned char)(i % 256);

        if (fwrite(&value, 1, 1, input) != 1)
        {
            perror("fwrite input");
            goto cleanup;
        }
    }

    fclose(input);
    input = NULL;

    /*
     * ------------------------------------------------------------
     * Step 2: Read the file through our file segmentation API.
     * ------------------------------------------------------------
     */
    if (file_sender_open(&sender, input_filename) != 0)
    {
        printf("Failed to open sender file\n");
        goto cleanup;
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
            goto cleanup;
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
        goto cleanup;
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
    input = fopen(output_filename, "rb");
    if (input != NULL)
    {
        fclose(input);
        input = NULL;
        fprintf(stderr, "Temporary test output already exists: %s\n",
                output_filename);
        goto cleanup;
    }

    if (file_receiver_open(&receiver, output_filename) != 0)
    {
        printf("Failed to open receiver file\n");
        goto cleanup;
    }
    output_created = 1;

    if (file_receiver_write_packet(&receiver, &packets[2]) != 0)
    {
        printf("Failed to write packet 2\n");
        goto cleanup;
    }

    if (file_receiver_write_packet(&receiver, &packets[0]) != 0)
    {
        printf("Failed to write packet 0\n");
        goto cleanup;
    }

    if (file_receiver_write_packet(&receiver, &packets[1]) != 0)
    {
        printf("Failed to write packet 1\n");
        goto cleanup;
    }

    file_receiver_close(&receiver);

    printf("Out-of-order reassembly completed.\n");

    /*
     * ------------------------------------------------------------
     * Step 4: Compare original and reconstructed files.
     * ------------------------------------------------------------
     */
    original = fopen(input_filename, "rb");
    reconstructed = fopen(output_filename, "rb");

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

        goto cleanup;
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
    original = NULL;
    fclose(reconstructed);
    reconstructed = NULL;

    if (different)
    {
        printf("FILE TEST FAILED\n");
        goto cleanup;
    }

    printf("FILE TEST PASSED\n");

    result = 0;

cleanup:
    if (input != NULL) fclose(input);
    if (original != NULL) fclose(original);
    if (reconstructed != NULL) fclose(reconstructed);
    file_sender_close(&sender);
    file_receiver_close(&receiver);
    if (input_created) remove(input_filename);
    if (output_created) remove(output_filename);
    return result;
}
