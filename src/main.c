#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <hidapi/hidapi.h>
#include "demux.h"

#define VENDOR_ID  0x16d0
#define PRODUCT_ID 0x0787

#define REPORT_SIZE_MAX 65 // Max possible size (64 bytes payload + 1 byte Report ID)

volatile int keep_running = 1;

// Handle Ctrl+C gracefully to close the USB interface
void sigint_handler(int dummy) {
    keep_running = 0;
}

// Debug: Print first N bytes of a packet in hex
static void dump_packet(const unsigned char *buf, int len, const char *label) {
    printf("[DEBUG %s] len=%d: ", label, len);
    int dump_len = len < 16 ? len : 16;
    for (int i = 0; i < dump_len; i++) {
        printf("%02x ", buf[i]);
    }
    if (len > 16) printf("...");
    printf("\n");
}

int main(int argc, char* argv[]) {
    hid_device *handle;
    unsigned char buf[REPORT_SIZE_MAX];
    uint8_t line1_audio[SAMPLES_PER_PACKET];
    int res;

    // Trap Ctrl+C
    signal(SIGINT, sigint_handler);

    if (hid_init() != 0) {
        fprintf(stderr, "Failed to initialize hidapi\n");
        return -1;
    }

    // Open the CID V6 Device
    handle = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    if (!handle) {
        fprintf(stderr, "Unable to open CID V6. Are you running with sudo? Check VID/PID.\n");
        hid_exit();
        return -1;
    }

    printf("[+] Sistemler CID V6 Connected.\n");

    // Set non-blocking mode so we can detect silence vs actual data
    if (hid_set_nonblocking(handle, 1) != 0) {
        fprintf(stderr, "[!] Warning: Could not set non-blocking mode.\n");
    } else {
        printf("[+] Non-blocking mode enabled.\n");
    }

    // Try to read device info
    wchar_t wstr[256];
    if (hid_get_manufacturer_string(handle, wstr, sizeof(wstr)/sizeof(wstr[0])) == 0) {
        printf("[+] Manufacturer: %ls\n", wstr);
    }
    if (hid_get_product_string(handle, wstr, sizeof(wstr)/sizeof(wstr[0])) == 0) {
        printf("[+] Product: %ls\n", wstr);
    }

    // Open a raw file to stream the audio into
    FILE *audio_out = fopen("/tmp/cid_line1.raw", "wb");
    if (!audio_out) {
        fprintf(stderr, "Failed to open output file.\n");
        hid_close(handle);
        return -1;
    }

    printf("[+] Streaming Line 1 audio to /tmp/cid_line1.raw\n");
    printf("[!] Make a test call now. Press Ctrl+C to stop.\n");
    printf("[DEBUG] Waiting for first packet...\n\n");

    int packet_count = 0;
    int silent_reads = 0;
    const int REPORT_INTERVAL = 250; // Print status every 250 packets (~1 second at 250Hz)

    // The Endless Hardware Polling Loop
    while (keep_running) {
        // With non-blocking mode, returns 0 if no data available
        res = hid_read(handle, buf, sizeof(buf));

        if (res < 0) {
            fprintf(stderr, "[-] Device disconnected or read error.\n");
            break;
        }

        if (res == 0) {
            silent_reads++;
            // Print status every ~4 seconds if nothing received
            if (silent_reads == 1000) {
                printf("[DEBUG] Still waiting for data... (no packets for ~4 sec)\n");
                silent_reads = 0;
            }
            usleep(4000); // Sleep for 4ms (polling rate)
            continue;
        }

        // We got data!
        silent_reads = 0;
        packet_count++;

        // Debug: Print first packet to inspect structure
        if (packet_count == 1) {
            dump_packet(buf, res, "FIRST PACKET");
            printf("[DEBUG] Packet interpretation:\n");

            // Check if this looks like 64-byte payload (no Report ID)
            if (res == 64) {
                printf("[DEBUG]   -> Exactly 64 bytes - likely NO Report ID (payload starts at index 0)\n");
            } else if (res == 65) {
                printf("[DEBUG]   -> 65 bytes - Report ID at index 0, payload at index 1\n");
            } else {
                printf("[DEBUG]   -> UNEXPECTED SIZE\n");
            }
        }

        // Progress indicator every second
        if (packet_count % REPORT_INTERVAL == 0) {
            printf("[DEBUG] Received %d packets (~%d seconds of audio)\n", packet_count, packet_count / 250);
        }

        // Determine payload start based on actual packet size
        // If we get exactly 64 bytes, there's probably no Report ID byte
        unsigned char *payload;

        if (res == 64) {
            // No Report ID - entire buffer is payload
            payload = &buf[0];
        } else if (res == 65) {
            // Report ID present at index 0
            payload = &buf[1];
        } else {
            // Unexpected size - try to handle gracefully
            printf("[WARN] Unexpected packet size: %d bytes\n", res);
            payload = &buf[0];
        }

        // Extract the 32 samples for Line 1
        demux_line1(payload, line1_audio);

        // Write the samples to our raw audio pipe/file
        fwrite(line1_audio, 1, SAMPLES_PER_PACKET, audio_out);
    }

    printf("\n[+] Shutting down gracefully...\n");

    fclose(audio_out);
    hid_close(handle);
    hid_exit();

    return 0;
}
