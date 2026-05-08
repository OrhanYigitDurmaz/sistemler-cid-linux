
# Sistemler CID-V6 Working Principle (How It Works)

This document explains how the Sistemler CID V6 Caller ID device transmits data to the host operating system and how to decode this data natively on Linux, completely bypassing the need for the proprietary, closed-source Windows library (`cid.dll`).

## 1. USB Connection and Data Flow
To avoid requiring users to install custom, signed OS drivers, the hardware presents itself to the host as a standard **USB HID (Human Interface Device)**. It essentially pretends to be a generic input device (like a keyboard or mouse) to leverage built-in OS protocols.

* **Endpoint:** HID Interrupt IN
* **Packet Size:** 64 Bytes payload (+1 Byte Report ID = 65 Bytes total buffer)
* **Polling Rate:** 4 milliseconds (250 packets per second)

## 2. Audio Compression and Multiplexing (The Magic)
The hardware faces a bandwidth bottleneck: it needs to push 4 separate telephone lines of 8000 Hz audio over a strict 64-byte USB limit. To achieve this, it sacrifices audio resolution (bit-depth) and uses a custom **4-bit Nibble-Packed PCM** algorithm.

Each 64-byte payload block is read in byte pairs, and the 4-bit halves (nibbles) of those bytes are routed as follows:

* **Even Bytes (Index 0, 2, 4...):**
  * Lower 4-bit (Lower Nibble) $\rightarrow$ **Line 1** Audio Sample
  * Upper 4-bit (Upper Nibble) $\rightarrow$ **Line 2** Audio Sample
* **Odd Bytes (Index 1, 3, 5...):**
  * Lower 4-bit (Lower Nibble) $\rightarrow$ **Line 3** Audio Sample
  * Upper 4-bit (Upper Nibble) $\rightarrow$ **Line 4** Audio Sample

**Conversion to 8-bit PCM:** To convert this raw, 4-bit packed data back into standard audio, the extracted values are bit-shifted to the left by 4 (`val << 4`). This scales the 0–15 range up to a 0–240 range. In this Unsigned PCM format, the value `128` represents absolute silence (the baseline).

## 3. FSK Demodulation (Decoding the Number)
Once the raw data is de-interleaved into a continuous, 8-bit, 8000 Hz audio wave, the analog Caller ID signals sent by the telephone exchange become visible. These signals can be decoded into text using standard open-source tools (like `minimodem`) with the following parameters:

* **Modulation Standard:** European V.23 (Frequency Shift Keying)
* **Baud Rate:** 1200 bps
* **Mark (1) Frequency:** 1300 Hz
* **Space (0) Frequency:** 2100 Hz
* **Data Format:** MDMF (Multiple Data Message Format)

**Important Note:** The hardware does not fully filter out the high-voltage (approx. 90V AC) ring signal from the analog telephone line. This will appear as a massive, clipping audio artifact in the stream immediately preceding the FSK data burst. Any decoding software must be robust enough to ignore this clipping.

## 4. Hardware Verification Logic (The Original DLL)
During reverse engineering, we discovered that the manufacturer uses a single "universal" DLL for multiple hardware revisions. The software decides which decoding algorithm to apply by comparing XOR-encrypted strings against the connected USB device path:

* **`T^S7a!`** decrypts to $\rightarrow$ **CIDv6** (This project, 8000 Hz)
* **`T^S7a"`** decrypts to $\rightarrow$ **CIDv5** (Legacy hardware, 7150 Hz)
