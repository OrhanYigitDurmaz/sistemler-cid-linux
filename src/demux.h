#ifndef DEMUX_H
#define DEMUX_H

#include <stdint.h>

#define PAYLOAD_SIZE 64

#define SAMPLES_PER_PACKET 32

// Extracts Line 1 (Lower nibble of even bytes)
void demux_line1(const uint8_t *payload, uint8_t *output);

// Extracts Line 2 (Upper nibble of even bytes)
void demux_line2(const uint8_t *payload, uint8_t *output);

// Extracts Line 3 (Lower nibble of odd bytes)
void demux_line3(const uint8_t *payload, uint8_t *output);

// Extracts Line 4 (Upper nibble of odd bytes)
void demux_line4(const uint8_t *payload, uint8_t *output);

// Generic demux for any line (1-4)
void demux_line(const uint8_t *payload, uint8_t *output, int line);

#endif // DEMUX_H
