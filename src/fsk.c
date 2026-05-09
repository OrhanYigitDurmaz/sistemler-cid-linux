#include "fsk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <spandsp.h>
#include <spandsp/adsi.h>

// Internal state structure
struct fsk_state_s {
    adsi_rx_state_t *adsi;     // spandsp ADSI receiver (handles CID)
    caller_id_t last_cid;       // Last received CID data
    int line_number;            // Phone line number (1-4)
    int quiet;                  // Suppress non-JSON output
    int new_data;               // Flag for new data
};

// Callback when ADSI message is received (caller ID data)
static void adsi_msg_callback(void *user_data, const uint8_t *msg, int len) {
    fsk_state_t *fsk = (fsk_state_t*)user_data;

    if (len < 3) {
        if (!fsk->quiet) printf("[FSK] Message too short (%d bytes)\n", len);
        return;
    }

    // ADSI message format:
    // msg[0] = message type
    // msg[1] = message length
    // msg[2...] = data words

    uint8_t msg_type = msg[0];
    uint8_t msg_len = msg[1];

    if (!fsk->quiet) printf("[FSK] ADSI message received - Type: 0x%02X, Length: %d\n", msg_type, msg_len);

    // Reset CID data
    memset(&fsk->last_cid, 0, sizeof(fsk->last_cid));
    fsk->last_cid.timestamp = time(NULL);
    fsk->last_cid.line_number = fsk->line_number;

    // Parse MDMF format (0x80) or SDMF format (0x04)
    if (msg_type == 0x80) {
        // MDMF - Multiple Data Message Format
        if (!fsk->quiet) printf("[FSK] MDMF format\n");

        int pos = 2;  // Start after message type and length
        while (pos < len) {
            if (pos + 1 >= len) break;

            uint8_t field_type = msg[pos];
            uint8_t field_len = msg[pos + 1];

            if (pos + 2 + field_len > len) break;

            const uint8_t *field_data = &msg[pos + 2];

            // Common field types for MDMF:
            // 0x01 - Date/time
            // 0x02 - Caller number
            // 0x07 - Caller name
            // 0x08 - Reason for absence

            if (field_type == 0x02 && field_len > 0 && field_len < sizeof(fsk->last_cid.phone_number)) {
                // Caller number
                memcpy(fsk->last_cid.phone_number, field_data, field_len);
                fsk->last_cid.phone_number[field_len] = '\0';
                if (!fsk->quiet) printf("[FSK] Phone number: %s\n", fsk->last_cid.phone_number);
            } else if (field_type == 0x07 && field_len > 0 && field_len < sizeof(fsk->last_cid.name)) {
                // Caller name
                memcpy(fsk->last_cid.name, field_data, field_len);
                fsk->last_cid.name[field_len] = '\0';
                if (!fsk->quiet) printf("[FSK] Caller name: %s\n", fsk->last_cid.name);
            } else if (field_type == 0x08) {
                // Reason for absence (private, unavailable, etc.)
                if (field_len > 0) {
                    if (!fsk->quiet) printf("[FSK] Number absent, reason: 0x%02X\n", field_data[0]);
                    strcpy(fsk->last_cid.phone_number, "private");
                }
            }

            pos += 2 + field_len;
        }
    } else if (msg_type == 0x04) {
        // SDMF - Single Data Message Format
        if (!fsk->quiet) printf("[FSK] SDMF format\n");

        // SDMF format: message type + length + date/time (8 bytes) + number (variable)
        // Date/time format: MMDDHHMM (4 bytes BCD)
        // Number follows immediately

        if (len > 10) {
            // Extract number (starts after 10 bytes)
            int num_len = len - 10;
            if (num_len > 0 && num_len < sizeof(fsk->last_cid.phone_number) - 1) {
                memcpy(fsk->last_cid.phone_number, &msg[10], num_len);
                fsk->last_cid.phone_number[num_len] = '\0';
                if (!fsk->quiet) printf("[FSK] Phone number: %s\n", fsk->last_cid.phone_number);
            }
        }
    } else {
        if (!fsk->quiet) printf("[FSK] Unknown message type: 0x%02X\n", msg_type);
    }

    fsk->last_cid.has_data = 1;
    fsk->new_data = 1;
}

// Initialize FSK demodulator for European V.23 CID
fsk_state_t* fsk_init(int line_num, int quiet) {
    if (line_num < 1 || line_num > 4) {
        if (!quiet) fprintf(stderr, "Invalid line number: %d (must be 1-4)\n", line_num);
        return NULL;
    }

    fsk_state_t *fsk = calloc(1, sizeof(struct fsk_state_s));
    if (!fsk) {
        if (!quiet) fprintf(stderr, "Failed to allocate FSK state\n");
        return NULL;
    }

    fsk->line_number = line_num;
    fsk->quiet = quiet;

    // Initialize ADSI receiver for ETSI CLIP (European caller ID)
    // ADSI_STANDARD_CLIP = ETSI CLIP (V.23 based, used in Europe)
    fsk->adsi = adsi_rx_init(NULL, ADSI_STANDARD_CLIP, adsi_msg_callback, fsk);
    if (!fsk->adsi) {
        if (!quiet) fprintf(stderr, "Failed to initialize ADSI receiver\n");
        free(fsk);
        return NULL;
    }

    if (!quiet) printf("[+] FSK demodulator initialized for Line %d (V.23, 1200 baud, ETSI CLIP)\n", line_num);
    return fsk;
}

// Feed audio samples to the demodulator
int fsk_feed_audio(fsk_state_t *fsk, const uint8_t *samples, size_t count) {
    if (!fsk || !samples) return 0;

    // Convert 8-bit unsigned PCM to spandsp format
    // spandsp expects int16_t signed PCM
    int16_t pcm[count];
    for (size_t i = 0; i < count; i++) {
        // Convert from unsigned 8-bit (0-255, center 128) to signed 16-bit
        pcm[i] = (int16_t)((int)samples[i] - 128) * 256;
    }

    // Feed samples to ADSI receiver
    adsi_rx(fsk->adsi, pcm, count);

    // Check if new data was received
    if (fsk->new_data) {
        fsk->new_data = 0;
        return 1;
    }

    return 0;
}

// Get the last received caller ID data
const caller_id_t* fsk_get_cid_data(fsk_state_t *fsk) {
    if (!fsk) return NULL;
    if (fsk->last_cid.has_data) {
        return &fsk->last_cid;
    }
    return NULL;
}

// Cleanup FSK demodulator
void fsk_cleanup(fsk_state_t *fsk) {
    if (!fsk) return;

    if (fsk->adsi) {
        adsi_rx_free(fsk->adsi);
    }
    free(fsk);
}
