#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <hidapi/hidapi.h>
#include "demux.h"

#define VENDOR_ID  0x16d0
#define PRODUCT_ID 0x0787

#define REPORT_SIZE 65 // 64 bytes payload + 1 byte Report ID

volatile int keep_running = 1;

// Handle Ctrl+C gracefully to close the USB interface
void sigint_handler(int dummy) {
    keep_running = 0;
}

int main(int argc, char* argv[]) {
    hid_device *handle;
    unsigned char buf[REPORT_SIZE];
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

    // Open a raw file to stream the audio into
    FILE *audio_out = fopen("/tmp/cid_line1.raw", "wb");
    if (!audio_out) {
        fprintf(stderr, "Failed to open output file.\n");
        hid_close(handle);
        return -1;
    }

    printf("[+] Streaming Line 1 audio to /tmp/cid_line1.raw\n");
    printf("[!] Make a test call now. Press Ctrl+C to stop.\n\n");

    // The Endless Hardware Polling Loop
    while (keep_running) {
        // Read blocks until a 4ms USB interrupt packet arrives
        res = hid_read(handle, buf, sizeof(buf));

        if (res < 0) {
            fprintf(stderr, "[-] Device disconnected or read error.\n");
            break;
        }

        if (res > 0) {
            // buf[0] is the HID Report ID. The payload starts at buf[1].
            unsigned char *payload = &buf[1];

            // Extract the 32 samples for Line 1
            demux_line1(payload, line1_audio);

            // Write the samples to our raw audio pipe/file
            fwrite(line1_audio, 1, SAMPLES_PER_PACKET, audio_out);
            fflush(audio_out);
        }
    }

    printf("\n[+] Shutting down gracefully...\n");

    fclose(audio_out);
    hid_close(handle);
    hid_exit();

    return 0;
}
