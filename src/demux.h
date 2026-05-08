#ifndef DEMUX_H
#define DEMUX_H

#include <stdint.h>

#define PAYLOAD_SIZE 64

#define SAMPLES_PER_PACKET 32

// Extracts Line 1 (Lower nibble of even bytes)
void demux_line1(const uint8_t *payload, uint8_t *output);

// Extracts Line 2 (Upper nibble of even bytes)
void demux_line2(const uint8_t *payload, uint8_t *output);

#endif // DEMUX_H
