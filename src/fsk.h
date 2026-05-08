#ifndef FSK_H
#define FSK_H

#include <stdint.h>
#include <time.h>

// Opaque FSK demodulator state (spandsp adsi_rx_state_t)
typedef struct fsk_state_s fsk_state_t;

// Caller ID data structure
typedef struct {
    time_t timestamp;
    char phone_number[32];
    char name[64];
    int has_data;  // 1 if valid data received, 0 otherwise
} caller_id_t;

// Initialize FSK demodulator for European V.23 CID
fsk_state_t* fsk_init(void);

// Feed audio samples to the demodulator
// Returns 1 if a complete caller ID message was received, 0 otherwise
int fsk_feed_audio(fsk_state_t *fsk, const uint8_t *samples, size_t count);

// Get the last received caller ID data
const caller_id_t* fsk_get_cid_data(fsk_state_t *fsk);

// Cleanup FSK demodulator
void fsk_cleanup(fsk_state_t *fsk);

#endif // FSK_H
