# Member 1 — PDCP Security Implementation Report

## Overview

This document records the implementation work done by Member 1 (Pair A) to replace the placeholder cryptographic algorithms in the PDCP layer with real ones: **AES-128-CTR** for ciphering and **HMAC-SHA256** for integrity protection.

All changes are backward-compatible through config flags. The V1 defaults (`cipher_algorithm=0`, `integrity_algorithm=0`) preserve the original XOR/CRC32 behavior exactly.

---

## What Was Changed

### Files Modified

| File | Change Summary |
|------|----------------|
| `include/common.h` | Added `cipher_algorithm` and `integrity_algorithm` fields to Config struct (default = 0) |
| `include/pdcp.h` | Added two new private method declarations: `apply_cipher_aes_ctr()`, `compute_mac_i_hmac()` |
| `src/pdcp.cpp` | Added AES-128-CTR and HMAC-SHA256 implementations; added config switches in `apply_cipher()` and `compute_mac_i()` |
| `tests/test_pdcp.cpp` | Added 9 new test cases and a profiling function |
| `CMakeLists.txt` | Added `find_package(OpenSSL REQUIRED)` and `target_link_libraries` for targets that link `pdcp.cpp` |

### Files NOT Touched (as per ownership rules)

`src/rlc.cpp`, `src/mac.cpp`, `src/main.cpp`, `src/ip_generator.cpp`, and all other pairs' test files.

---

## Implementation Details

### 1. Config Fields (common.h)

```cpp
uint8_t cipher_algorithm    = 0;  // 0 = XOR (V1), 1 = AES-128-CTR
uint8_t integrity_algorithm = 0;  // 0 = CRC32 (V1), 1 = HMAC-SHA256
```

Defaults are 0, so all existing code and tests behave identically to V1 without any changes.

### 2. AES-128-CTR Ciphering (pdcp.cpp)

**Algorithm:** AES-128 in Counter (CTR) mode, per TS 38.323 Section 5.8 (NEA2 simplified).

**IV Construction (16 bytes):**
```
IV = COUNT(4 bytes) || BEARER(1 byte, 0x01) || DIRECTION(1 byte, 0x00) || zeros(10 bytes)
```

**API Used:** OpenSSL EVP interface:
- `EVP_CIPHER_CTX_new()` / `EVP_CIPHER_CTX_free()` for context management
- `EVP_EncryptInit_ex()` with `EVP_aes_128_ctr()` for initialization
- `EVP_EncryptUpdate()` / `EVP_EncryptFinal_ex()` for encryption

**Key Property:** AES-CTR is self-inverse — the same operation encrypts and decrypts, matching the behavior of the V1 XOR cipher. The existing `process_tx`/`process_rx` flow (cipher before MAC-I on TX, verify MAC-I before decipher on RX) is preserved.

**Wiring:** The existing `apply_cipher()` method now checks `config_.cipher_algorithm`:
- `0` -> original XOR stream cipher (V1 code unchanged)
- `1` -> calls `apply_cipher_aes_ctr()`

### 3. HMAC-SHA256 Integrity (pdcp.cpp)

**Algorithm:** HMAC with SHA-256, per TS 38.323 Section 5.9 (NIA2 simplified). The 32-byte HMAC output is truncated to 4 bytes to fit the MAC-I field.

**Integrity Input Construction:**
```
message = COUNT(4 bytes) || BEARER(1 byte, 0x01) || DIRECTION(1 byte, 0x00) || ciphered_payload
```

**API Used:** OpenSSL `HMAC()` function with `EVP_sha256()`.

**Wiring:** The existing `compute_mac_i()` method now checks `config_.integrity_algorithm`:
- `0` -> original CRC32 integrity (V1 code unchanged)
- `1` -> calls `compute_mac_i_hmac()`

### 4. OpenSSL Dependency (CMakeLists.txt)

Added `find_package(OpenSSL REQUIRED)` and linked `OpenSSL::SSL OpenSSL::Crypto` to:
- `5g_layer2` (main executable)
- `test_pdcp` (PDCP unit tests)
- `test_integration` (integration tests)

`test_rlc` and `test_mac` are unchanged — they don't link `pdcp.cpp`.

**Prerequisite:** `sudo apt install libssl-dev` on Ubuntu/WSL.

---

## Tests Added

9 new tests added to `test_pdcp.cpp`, all placed AFTER the existing 6 tests:

| # | Test Name | What It Verifies |
|---|-----------|------------------|
| 7 | `test_aes_roundtrip_12bit` | AES-128-CTR encrypt/decrypt round-trip with 12-bit SN |
| 8 | `test_hmac_roundtrip_12bit` | HMAC-SHA256 integrity round-trip with 12-bit SN |
| 9 | `test_aes_hmac_combined` | Both AES + HMAC enabled simultaneously |
| 10 | `test_aes_actually_encrypts` | Ciphertext differs from plaintext (AES is working) |
| 11 | `test_hmac_wrong_key_fails` | Wrong integrity key causes `process_rx` to return empty buffer |
| 12 | `test_xor_vs_aes_differ` | XOR and AES produce different ciphertext for same input |
| 13 | `test_aes_hmac_18bit` | 18-bit SN mode works with new algorithms |
| 14 | `test_aes_hmac_max_sdu` | 9000-byte payload (max PDCP SDU) works |
| 15 | `test_aes_hmac_sequential` | 20 sequential packets with incrementing COUNT values |

**Result: 15/15 PDCP tests pass. All 23 original V1 tests across all layers pass.**

---

## Profiling Results

Profiling was run with 1000 iterations per configuration. All times in microseconds (average per packet).

### Cipher Variant Comparison (integrity held constant at CRC32)

| Pkt Size | XOR TX (us) | XOR RX (us) | AES TX (us) | AES RX (us) | AES Speedup |
|----------|-------------|-------------|-------------|-------------|-------------|
| 100 | 2.77 | 2.61 | 2.50 | 2.55 | 1.1x |
| 500 | 8.18 | 7.88 | 4.22 | 4.27 | 1.9x |
| 1000 | 14.17 | 13.99 | 6.54 | 6.71 | 2.1x |
| 1400 | 19.07 | 18.98 | 8.39 | 8.45 | 2.3x |
| 3000 | 40.59 | 40.29 | 14.20 | 14.38 | 2.8x |
| 9000 | 107.81 | 107.77 | 38.55 | 38.27 | 2.8x |

**Finding:** AES-128-CTR is significantly faster than the V1 XOR cipher at all packet sizes, with the speedup increasing with payload size (up to ~2.8x at 9000 bytes). This is because OpenSSL leverages hardware AES-NI instructions, while the V1 XOR cipher is a naive byte-by-byte loop.

### Integrity Variant Comparison (cipher held constant at XOR)

| Pkt Size | CRC32 TX (us) | CRC32 RX (us) | HMAC TX (us) | HMAC RX (us) | HMAC Speedup |
|----------|---------------|---------------|--------------|--------------|--------------|
| 100 | 2.59 | 2.50 | 4.32 | 4.10 | 0.6x (slower) |
| 500 | 7.70 | 7.17 | 8.22 | 8.02 | 0.9x |
| 1000 | 12.94 | 12.78 | 13.10 | 12.99 | 1.0x |
| 1400 | 18.26 | 17.72 | 17.07 | 16.64 | 1.1x |
| 3000 | 37.30 | 36.91 | 32.97 | 32.18 | 1.1x |
| 9000 | 106.66 | 105.44 | 92.00 | 91.74 | 1.2x |

**Finding:** HMAC-SHA256 has higher per-call overhead for small packets (due to SHA-256 initialization) but becomes faster than CRC32 at larger packet sizes (1400+ bytes). At 9000 bytes, HMAC-SHA256 is ~15% faster because OpenSSL's SHA-256 implementation is hardware-optimized, while our CRC32 uses a simple lookup table.

---

## Design Decisions

1. **Config flags with V1 defaults:** Ensures zero risk of breaking existing functionality. Any test or integration that doesn't explicitly set the new fields gets V1 behavior.

2. **Separate methods, not inline code:** `apply_cipher_aes_ctr()` and `compute_mac_i_hmac()` are separate private methods called from the existing dispatchers. This keeps the code clean and makes it easy to add more algorithms later.

3. **Same TX/RX order preserved:** MAC-I is computed over the ciphered payload on both TX and RX, matching the V1 invariant. This is also consistent with TS 38.323.

4. **HMAC truncation to 4 bytes:** The full 32-byte HMAC-SHA256 output is truncated to 4 bytes to fit the existing MAC-I field size in the PDU format. This avoids any PDU format changes.

---

## How to Build and Test

```bash
# Prerequisites
sudo apt install libssl-dev

# Build
cd build
cmake ..
make -j$(nproc)

# Run PDCP tests (15 tests + profiling)
./test_pdcp

# Run all tests (verify no regression)
./test_pdcp && ./test_rlc && ./test_mac && ./test_integration

# To use new algorithms in main simulator, modify Config in main.cpp:
#   cfg.cipher_algorithm = 1;    // AES-128-CTR
#   cfg.integrity_algorithm = 1; // HMAC-SHA256
```
