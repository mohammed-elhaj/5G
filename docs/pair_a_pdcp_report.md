# Section 2: PDCP Layer — Security and Header Compression

## 2.1 Introduction

The PDCP (Packet Data Convergence Protocol) layer in our 5G NR Layer 2 simulator handles three key functions per 3GPP TS 38.323: header compression, ciphering, and integrity protection. The V1 implementation used placeholder algorithms (XOR stream cipher and CRC32 integrity) that demonstrated the correct data flow but lacked cryptographic strength. Pair A's work replaced these with production-grade algorithms and added optional header compression.

**Division of work:**
- **Member 1** — Replaced XOR cipher with AES-128-CTR and CRC32 integrity with HMAC-SHA256
- **Member 2** — Added simplified ROHC-style IPv4 header compression

Both features are controlled by config flags with defaults that preserve exact V1 behavior.

## 2.2 Member 1: AES-128-CTR Ciphering (NEA2)

### Design

Per TS 38.323 Section 5.8, the 5G NR standard uses NEA2 (AES-128 in CTR mode) for ciphering. We implemented this using the OpenSSL EVP API.

**IV construction:** `COUNT(4B) || BEARER(1B) || DIRECTION(1B) || zeros(10B)` — 16 bytes total, matching the TS 38.323 ciphering input parameters.

AES-CTR is self-inverse: the same operation encrypts and decrypts, maintaining the same `apply_cipher()` interface as the V1 XOR cipher.

**Config flag:** `cipher_algorithm` — 0 = XOR (V1 default), 1 = AES-128-CTR

### Implementation

```cpp
void PdcpLayer::apply_cipher_aes_ctr(std::vector<uint8_t>& data, uint32_t count) {
    uint8_t iv[16] = {0};
    iv[0..3] = count (big-endian);
    iv[4] = BEARER_ID;
    iv[5] = DIRECTION;
    // OpenSSL EVP_EncryptInit_ex + EVP_EncryptUpdate + EVP_EncryptFinal_ex
}
```

### Performance

| Packet Size | XOR TX (μs) | AES TX (μs) | Speedup |
|-------------|-------------|-------------|---------|
| 100 | 3.06 | 2.54 | 1.2x faster |
| 500 | 8.15 | 4.30 | 1.9x faster |
| 1000 | 13.65 | 6.16 | 2.2x faster |
| 1400 | 18.24 | 7.62 | 2.4x faster |
| 3000 | 36.91 | 14.64 | 2.5x faster |
| 9000 | 109.01 | 39.46 | 2.8x faster |

AES-128-CTR is significantly faster than our V1 XOR cipher for packets above 100 bytes, thanks to hardware AES-NI support. The XOR cipher performs a modular key expansion per byte, while AES-CTR uses hardware-accelerated block operations.

## 2.3 Member 1: HMAC-SHA256 Integrity (NIA2)

### Design

Per TS 38.323 Section 5.9, we implemented HMAC-SHA256 using OpenSSL's `HMAC()` function. The integrity input includes `COUNT || BEARER || DIRECTION || payload`. The 32-byte HMAC output is truncated to 4 bytes to fit the MAC-I field.

**Config flag:** `integrity_algorithm` — 0 = CRC32 (V1 default), 1 = HMAC-SHA256

### Performance

| Packet Size | CRC32 TX (μs) | HMAC TX (μs) | Ratio |
|-------------|---------------|--------------|-------|
| 100 | 2.58 | 4.45 | 1.7x slower |
| 500 | 7.83 | 8.31 | 1.1x slower |
| 1000 | 12.64 | 13.87 | 1.1x slower |
| 1400 | 18.34 | 17.35 | 1.1x faster |
| 3000 | 36.99 | 33.95 | 1.1x faster |
| 9000 | 107.34 | 92.50 | 1.2x faster |

HMAC-SHA256 has higher setup cost (visible at small packet sizes) but scales better than CRC32 for large packets due to optimized SHA-256 implementations in OpenSSL.

## 2.4 Member 2: Header Compression

### Design

Per TS 38.323 Section 5.6, PDCP supports header compression using ROHC (RFC 5795). Our simplified implementation exploits the fact that in a stream of packets between the same endpoints, 14 of the 20 IPv4 header bytes are static (src/dst IP, protocol, TTL, flags, version, DSCP). Only 6 bytes change per packet: Total Length, Identification, and Header Checksum.

**Compression approach:**
1. **First packet** — sent uncompressed to establish context (static fields stored by both compressor and decompressor)
2. **Subsequent packets** — 20-byte IPv4 header replaced with 7-byte compressed header:

```
[0xFC marker (1B)] [Total Length (2B)] [Identification (2B)] [Checksum (2B)]
```

**Result:** 13 bytes saved per packet after context establishment.

**Pipeline placement:** Compression before ciphering on TX, decompression after deciphering on RX — so cipher and integrity operate on the smaller compressed payload.

### Performance

| Packet Size | Compression OFF PDU | Compression ON PDU | Saved | TX Overhead |
|-------------|--------------------|--------------------|-------|-------------|
| 100 B | 106 B | 93 B | 13 B (12.3%) | +0.37 μs |
| 500 B | 506 B | 493 B | 13 B (2.6%) | +0.36 μs |
| 1400 B | 1406 B | 1393 B | 13 B (0.9%) | +0.46 μs |
| 9000 B | 9006 B | 8993 B | 13 B (0.1%) | +1.10 μs |

Compression adds minimal processing overhead (<1 μs for typical packet sizes) while consistently saving 13 bytes per packet.

## 2.5 Combined Feature Testing

All three features (AES-128-CTR + HMAC-SHA256 + compression) work together correctly. The test `Compression + AES-128-CTR + HMAC-SHA256 combined` verifies lossless round-trip for 10 consecutive 1400-byte packets with all features enabled simultaneously.

## 2.6 Test Summary

| Category | Tests | Status |
|----------|-------|--------|
| V1 original tests (compression/cipher/integrity OFF defaults) | 6 | All pass |
| Member 1: AES-128-CTR and HMAC-SHA256 | 9 | All pass |
| Member 2: Header compression | 8 | All pass |
| **Total PDCP tests** | **23** | **All pass** |
| Integration tests (full pipeline) | 7 | All pass |

## 2.7 Files Modified

| File | Member | Changes |
|------|--------|---------|
| `CMakeLists.txt` | 1 | Added OpenSSL dependency (`find_package`, link `ssl crypto`) |
| `include/common.h` | 1 | Added `cipher_algorithm` and `integrity_algorithm` config fields |
| `include/pdcp.h` | 1+2 | Added AES/HMAC method declarations, `CompressionContext` struct |
| `src/pdcp.cpp` | 1+2 | AES-128-CTR cipher, HMAC-SHA256 integrity, compress/decompress |
| `tests/test_pdcp.cpp` | 1+2 | 17 new tests + profiling tables |

No public interface changes. No other layers affected.

## 2.8 References

- 3GPP TS 38.323 — PDCP specification (Sections 5.6, 5.7, 5.8, 5.9)
- RFC 5795 — The Robust Header Compression (ROHC) Framework
- OpenSSL EVP API — AES-128-CTR implementation
- OpenSSL HMAC API — HMAC-SHA256 implementation
