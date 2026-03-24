# Member 2 — PDCP Header Compression Implementation Report

## Overview

Implemented simplified ROHC-style IPv4 header compression in the PDCP layer, per TS 38.323 Section 5.6. The compression reduces per-packet overhead by exploiting the fact that most IPv4 header fields remain constant across packets in the same flow.

## Design

### Problem

Every IP packet carries a 20-byte IPv4 header. In a stream between the same endpoints using the same protocol, 14 of those 20 bytes (src/dst IP, protocol, TTL, flags, version, DSCP) are identical across every packet. Only 6 bytes change: Total Length (2B), Identification (2B), and Header Checksum (2B).

### Solution — Context-Based Compression

Inspired by ROHC (RFC 5795), the compressor and decompressor maintain shared "context" — the static header fields stored from the first packet. After context establishment, subsequent packets replace the full 20-byte header with a 7-byte compressed header carrying only the dynamic fields.

**Compressed header format (7 bytes):**

| Byte | Field | Source |
|------|-------|--------|
| 0 | `0xFC` marker | Fixed (distinguishes compressed from uncompressed) |
| 1-2 | Total Length | IPv4 bytes 2-3 (dynamic) |
| 3-4 | Identification | IPv4 bytes 4-5 (dynamic) |
| 5-6 | Header Checksum | IPv4 bytes 10-11 (dynamic) |

**Result:** 20 bytes → 7 bytes = **13 bytes saved per packet** after context establishment.

### Context Storage

Static fields are stored in a `CompressionContext` struct:

```cpp
struct CompressionContext {
    bool     context_established = false;
    uint8_t  version_ihl;        // byte 0  (always 0x45)
    uint8_t  dscp_ecn;           // byte 1  (always 0x00)
    uint16_t flags_fragment;     // bytes 6-7 (always 0x4000)
    uint8_t  ttl;                // byte 8  (always 64)
    uint8_t  protocol;           // byte 9  (always 17/UDP)
    uint32_t src_ip;             // bytes 12-15
    uint32_t dst_ip;             // bytes 16-19
};
```

Separate TX and RX contexts ensure a single `PdcpLayer` instance can handle both directions independently.

### Pipeline Integration

Per TS 38.323, compression occurs **before** ciphering on TX, and decompression occurs **after** deciphering on RX:

```
TX: SDU → compress_header → cipher → compute MAC-I → PDU
RX: PDU → verify MAC-I → decipher → decompress_header → SDU
```

This means ciphering and integrity operate on the compressed (smaller) payload, which is also a minor performance benefit.

### Safety Properties

- **First packet** is always sent uncompressed (context establishment, like ROHC IR state)
- **Non-IPv4 packets** (first byte ≠ 0x45) pass through unmodified
- **Packets < 20 bytes** pass through unmodified
- **`compression_enabled = false`** (default) disables all compression logic — exact V1 behavior preserved
- **`reset()`** clears both TX and RX contexts

## Files Modified

| File | Changes |
|------|---------|
| `include/pdcp.h` | Added `CompressionContext` struct, `tx_comp_ctx_`/`rx_comp_ctx_` members, constants (`COMPRESSED_MARKER`, `IPV4_HEADER_SIZE`, `COMPRESSED_HEADER_SIZE`) |
| `src/pdcp.cpp` | Implemented `compress_header()` and `decompress_header()`, wired into `process_tx`/`process_rx`, updated `reset()` |
| `tests/test_pdcp.cpp` | Added `make_ipv4_packet()` helper, 8 compression tests, compression profiling |

## Test Results

All 8 compression tests pass:

| Test | What it verifies |
|------|------------------|
| Compression round-trip (10 packets) | Full TX→RX pipeline with compression produces identical SDUs |
| First packet sent uncompressed | Context establishment packet has same size as uncompressed |
| Second packet is smaller (13 bytes saved) | Exactly 13 bytes saved per compressed packet |
| Compression disabled | `compression_enabled=false` leaves data unchanged |
| Compression + AES-128-CTR + HMAC-SHA256 | All three features work together |
| Compression with 18-bit SN | Works with both SN lengths |
| Non-IPv4 packet passes through | Non-0x45 packets are not compressed |
| Compression with 9000-byte packet | Works at maximum PDCP SDU size |

## Profiling Results

| Packet Size | Compression | TX avg (μs) | RX avg (μs) | PDU Size | Saved |
|-------------|-------------|-------------|-------------|----------|-------|
| 100 | OFF | 2.51 | 2.52 | 106 | - |
| 100 | ON | 2.88 | 3.21 | 93 | 13 B |
| 500 | OFF | 7.17 | 6.96 | 506 | - |
| 500 | ON | 7.53 | 7.93 | 493 | 13 B |
| 1000 | OFF | 12.81 | 12.87 | 1006 | - |
| 1000 | ON | 13.68 | 13.93 | 993 | 13 B |
| 1400 | OFF | 17.51 | 17.55 | 1406 | - |
| 1400 | ON | 17.97 | 18.36 | 1393 | 13 B |
| 9000 | OFF | 107.92 | 106.84 | 9006 | - |
| 9000 | ON | 109.02 | 109.40 | 8993 | 13 B |

**Observations:**
- Compression adds minimal overhead (~0.5 μs per packet)
- Saves a constant 13 bytes per packet regardless of packet size
- The relative benefit is highest for small packets (13/100 = 13% for 100B packets)

## References

- 3GPP TS 38.323, Section 5.6 — Header compression and decompression
- RFC 5795 — The Robust Header Compression (ROHC) Framework
