# Architecture & Data Flow

This document explains how data moves through the 5G NR Layer 2 simulator, what each layer does to the bytes, and what simplifications we made compared to the full 3GPP specifications.

---

## Overview

The simulator models the **UE-side uplink and downlink** processing of a single data radio bearer (DRB). An IP packet enters the top of the stack, gets transformed by three protocol layers, and emerges as a Transport Block. We then loop that Transport Block back into the receive side and verify the original IP packet is recovered byte-for-byte.

```
    IP Packet (1400 bytes)
         |
    [ PDCP TX ]  adds 2-byte header + 4-byte MAC-I + ciphers payload
         |
    PDCP PDU (1406 bytes)
         |
    [ RLC TX ]   segments into chunks that fit the MAC grant
         |
    3x RLC PDUs (474 + 469 + 469 bytes, with headers)
         |
    [ MAC TX ]   adds subheaders, packs into Transport Block, pads
         |
    Transport Block (2048 bytes)
         |
    ======= LOOPBACK =======
         |
    [ MAC RX ]   parses subheaders, extracts RLC PDUs
         |
    [ RLC RX ]   reassembles segments into complete PDCP PDU
         |
    [ PDCP RX ]  verifies integrity, deciphers, strips header
         |
    IP Packet (1400 bytes)  <-- verified identical to original
```

---

## Layer 1: PDCP (Packet Data Convergence Protocol)

**Spec reference:** TS 38.323

PDCP sits at the top of Layer 2. It handles security (ciphering and integrity protection) and, in the full spec, header compression. We skip header compression in V1.

### What PDCP TX Does

1. **Assign a sequence number (COUNT)** -- a monotonically increasing counter, starting at 0
2. **Cipher the payload** -- XOR each byte with a keystream derived from the cipher key and COUNT
3. **Compute integrity tag (MAC-I)** -- CRC32 over (integrity_key + COUNT + ciphered_payload), producing 4 bytes
4. **Build the PDCP PDU** -- prepend the header, append MAC-I

### What PDCP RX Does

1. **Parse the header** to extract the SN
2. **Verify MAC-I** -- recompute CRC32 and compare to the received tag
3. **Decipher the payload** -- XOR is its own inverse, so the same cipher function deciphers

### PDCP PDU Format (12-bit SN)

```
Byte:  0          1          2 .............. N      N+1  N+2  N+3  N+4
     +----------+----------+------------------+------+------+------+------+
     |D/C|R|R|R | SN[11:8] |   SN[7:0]       |        Ciphered          |
     | 1 |0|0|0 | (4 bits) |   (8 bits)      |        Payload           |
     +----------+----------+------------------+------+------+------+------+
     |<-- header (2 bytes) -->|<-- payload -->|<-- MAC-I (4 bytes) -->|
```

- **D/C = 1**: This is a data PDU (not a control PDU)
- **SN**: 12-bit sequence number (supports up to 4096 before wrapping)
- **Ciphered Payload**: The IP packet XORed with the keystream
- **MAC-I**: 4-byte CRC32 integrity tag (omitted if integrity is disabled)

### PDCP PDU Format (18-bit SN)

```
Byte:  0          1          2          3 .............. N+1  N+2  N+3  N+4
     +----------+----------+----------+------------------+-------------------+
     |D/C|R     | SN[17:12]| SN[11:4] | SN[3:0] |R(4b) |   Ciphered        |
     | 1 |0     | (6 bits) | (8 bits) | (4 bits)|0000  |   Payload          |
     +----------+----------+----------+------------------+-------------------+
     |<------- header (3 bytes) ------>|<-- payload ---->|<-- MAC-I (4B) --->|
```

### Simplified Ciphering

The full 3GPP spec uses NEA1 (SNOW 3G), NEA2 (AES-CTR), or NEA3 (ZUC). We use a simple XOR stream cipher:

```
keystream[i] = cipher_key[i % 16] XOR ((COUNT + i) mod 256)
ciphertext[i] = plaintext[i] XOR keystream[i]
```

This is **not cryptographically secure** but demonstrates the concept of a reversible stream cipher. The same function ciphers and deciphers because XOR is its own inverse.

### Simplified Integrity

The full spec uses NIA algorithms (CMAC-based). We use CRC32 over the concatenation of:
- The 16-byte integrity key
- The 4-byte COUNT value (big-endian)
- The ciphered payload

This produces a 4-byte MAC-I tag.

---

## Layer 2: RLC (Radio Link Control) -- UM Mode

**Spec reference:** TS 38.322

RLC handles segmentation (splitting large PDUs into pieces that fit the radio grant) and reassembly (stitching them back together). We implement **Unacknowledged Mode (UM)** only, which means no retransmission.

### What RLC TX Does (Segmentation)

1. If the SDU (PDCP PDU) fits within `rlc_max_pdu_size` including the 1-byte header, send it as a **complete SDU** (SI = 00)
2. Otherwise, chop it into segments:
   - **First segment** (SI = 01): 1-byte header, no Segment Offset field
   - **Middle segments** (SI = 11): 3-byte header (includes 16-bit Segment Offset)
   - **Last segment** (SI = 10): 3-byte header (includes 16-bit Segment Offset)

### What RLC RX Does (Reassembly)

1. Parse the SI and SN from each received PDU
2. Buffer segments by SN
3. When a complete set of segments arrives (first through last, with contiguous offsets), concatenate the data portions in offset order

### RLC UM PDU Formats (6-bit SN)

**Complete SDU (SI = 00):**
```
Byte:  0                    1 ................. N
     +----+------+         +--------------------+
     | SI | SN   |         |       Data         |
     | 00 |(6 b) |         | (complete SDU)     |
     +----+------+         +--------------------+
     |<- 1 byte ->|
```

**First Segment (SI = 01):**
```
Byte:  0                    1 ................. N
     +----+------+         +--------------------+
     | SI | SN   |         |     Data           |
     | 01 |(6 b) |         | (first part)       |
     +----+------+         +--------------------+
```

**Middle Segment (SI = 11):**
```
Byte:  0                    1         2         3 ............. N
     +----+------+         +---------+---------+-----------------+
     | SI | SN   |         |    SO (16-bit)    |     Data        |
     | 11 |(6 b) |         |   Segment Offset  | (middle part)   |
     +----+------+         +---------+---------+-----------------+
     |<- 1 byte ->|        |<--- 2 bytes ----->|
```

**Last Segment (SI = 10):**
```
Byte:  0                    1         2         3 ............. N
     +----+------+         +---------+---------+-----------------+
     | SI | SN   |         |    SO (16-bit)    |     Data        |
     | 10 |(6 b) |         |   Segment Offset  | (last part)     |
     +----+------+         +---------+---------+-----------------+
```

### Key NR vs LTE Difference

In NR (5G), RLC does **not** concatenate multiple SDUs into one PDU. Each RLC PDU carries exactly one SDU or one SDU segment. This simplifies our implementation significantly compared to LTE.

### Segmentation Example

A 1406-byte PDCP PDU with `rlc_max_pdu_size = 500`:

| Segment | SI | Header | SO | Data Size | Total PDU |
|---------|----|--------|----|-----------|-----------|
| First   | 01 | 1 byte | -- | 499 bytes | 500 bytes |
| Middle  | 11 | 3 bytes| 499| 497 bytes | 500 bytes |
| Last    | 10 | 3 bytes| 996| 410 bytes | 413 bytes |

---

## Layer 3: MAC (Medium Access Control)

**Spec reference:** TS 38.321

MAC is the lowest Layer 2 sublayer. It multiplexes data from one or more logical channels into a single Transport Block and adds padding to fill the block.

### What MAC TX Does (Multiplexing)

1. For each MAC SDU (RLC PDU), write a **subheader** immediately before the payload:
   - 1 byte: `[R=0][F][LCID(6 bits)]`
   - If F=0: 1 byte length (SDU <= 255 bytes)
   - If F=1: 2 bytes length (SDU > 255 bytes, big-endian)
2. Copy the SDU payload after its subheader
3. If space remains in the Transport Block, write a **padding subheader** (LCID = 63) and fill the rest with zeros

### What MAC RX Does (Demultiplexing)

1. Walk through the Transport Block byte by byte
2. Read each subheader: extract LCID and length
3. If LCID = 63, stop (rest is padding)
4. Extract the SDU payload and deliver it

### MAC PDU Format (NR style -- subheaders inline)

```
+------------+-----------+------------+-----------+-----+----------+---------+
| Subheader1 | Payload1  | Subheader2 | Payload2  | ... | Pad Hdr  | Padding |
| R|F|LCID|L | (SDU)     | R|F|LCID|L | (SDU)     |     | LCID=63  | 0x00... |
+------------+-----------+------------+-----------+-----+----------+---------+
|<---------------------- Transport Block Size (e.g., 2048 bytes) ----------->|
```

**Subheader format:**
```
Byte 0: [R(1)=0] [F(1)] [LCID(6)]
         |         |       |
         |         |       +-- Logical Channel ID (4 = DTCH for first DRB)
         |         +---------- Format: 0 = 8-bit L, 1 = 16-bit L
         +-------------------- Reserved, always 0

If F=0:  Byte 1:    L (8 bits)     -- SDU length, max 255
If F=1:  Byte 1-2:  L (16 bits)    -- SDU length, big-endian, max 65535
```

### NR vs LTE MAC Difference

In LTE, all subheaders are grouped at the front of the MAC PDU. In NR, each subheader is placed **immediately before** its payload. This makes parsing simpler because you process the TB as a stream of (subheader, payload) pairs.

---

## The Loopback

The loopback is intentionally simple. The Transport Block produced by MAC TX is passed directly to MAC RX without any physical layer simulation, channel modeling, or HARQ processing. This lets us verify that the protocol stack correctly transforms and recovers data, independent of radio channel effects.

```
MAC TX output (Transport Block)
         |
         +--- direct memory pass (no channel) --->
         |
MAC RX input (Transport Block)
```

---

## What We Simplified (and Why)

| Feature | Full 3GPP | Our V1 | Why |
|---------|-----------|--------|-----|
| Header compression | ROHC (RFC 5795) | Skipped | Complex, orthogonal to Layer 2 protocol learning |
| Ciphering | NEA1/NEA2/NEA3 | XOR stream cipher | Demonstrates the concept without crypto library dependencies |
| Integrity | NIA1/NIA2/NIA3 | CRC32 | Produces a 4-byte tag like the real thing, but simpler |
| RLC mode | TM, UM, AM | UM only | AM requires retransmission (ARQ) and STATUS PDUs, significant complexity |
| RLC reordering | t-Reassembly timer, sliding window | In-order only | Segments arrive in order in loopback, so reordering isn't needed |
| MAC scheduling | Dynamic grants, LCP across channels | Fixed grant, single channel | Scheduling is a PHY/scheduler concern, not core Layer 2 logic |
| MAC CEs | BSR, PHR, DRX commands | None | Control elements are important in practice but don't affect the data path |
| HARQ | Soft combining, 16 processes | Not implemented | Requires physical layer interaction |
| Multiple bearers | Multiple DRBs, SRBs | Single DRB (LCID 4) | One bearer is enough to demonstrate the full pipeline |

These simplifications keep the V1 implementation focused on the core data transformation pipeline. Each simplification is a candidate for a post-V1 optimization task (see [team_tasks.md](team_tasks.md)).
