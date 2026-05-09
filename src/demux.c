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

void demux_line3(const uint8_t *payload, uint8_t *output) {
    for (int i = 0; i < SAMPLES_PER_PACKET; i++) {
        // Line 3 and 4 live in the Odd bytes (1, 3, 5, 7...)
        uint8_t byte_odd = payload[i * 2 + 1];

        // Isolate the lower 4 bits (0x0F) and scale up to 8-bit (<< 4)
        output[i] = (byte_odd & 0x0F) << 4;
    }
}

void demux_line4(const uint8_t *payload, uint8_t *output) {
    for (int i = 0; i < SAMPLES_PER_PACKET; i++) {
        uint8_t byte_odd = payload[i * 2 + 1];

        // Isolate the upper 4 bits (0xF0).
        output[i] = (byte_odd & 0xF0);
    }
}

void demux_line(const uint8_t *payload, uint8_t *output, int line) {
    switch (line) {
        case 1:
            demux_line1(payload, output);
            break;
        case 2:
            demux_line2(payload, output);
            break;
        case 3:
            demux_line3(payload, output);
            break;
        case 4:
            demux_line4(payload, output);
            break;
    }
}
