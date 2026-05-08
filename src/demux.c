#include "demux.h"

void demux_line1(const uint8_t *payload, uint8_t *output) {
    for (int i = 0; i < SAMPLES_PER_PACKET; i++) {
        // Line 1 and 2 live in the Even bytes (0, 2, 4, 6...)
        uint8_t byte_even = payload[i * 2];

        // Isolate the lower 4 bits (0x0F) and scale up to 8-bit (<< 4)
        output[i] = (byte_even & 0x0F) << 4;
    }
}

void demux_line2(const uint8_t *payload, uint8_t *output) {
    for (int i = 0; i < SAMPLES_PER_PACKET; i++) {
        uint8_t byte_even = payload[i * 2];

        // Isolate the upper 4 bits (0xF0).
        // No need to bit-shift, they are already in the high position!
        output[i] = (byte_even & 0xF0);
    }
}
