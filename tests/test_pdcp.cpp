// ============================================================
// test_pdcp.cpp — Unit test for the PDCP layer
//
// Tests the PDCP TX → RX round-trip:
//   1. Generate a known payload
//   2. Pass through process_tx (cipher + integrity + header)
//   3. Pass the resulting PDU through process_rx
//   4. Verify the recovered payload matches the original byte-for-byte
//
// Also tests:
//   - Multiple sequential packets (SN increments correctly)
//   - 12-bit and 18-bit SN modes
//   - Ciphering/integrity disabled paths
// ============================================================

#include "pdcp.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <string>

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  TEST: " << name << " ... "; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "PASS\n"; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAIL: " << msg << "\n"; \
    } while(0)

/// Create a test SDU filled with a recognizable pattern
static ByteBuffer make_test_sdu(size_t size, uint8_t seed) {
    ByteBuffer sdu;
    sdu.data.resize(size);
    for (size_t i = 0; i < size; i++) {
        sdu.data[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    }
    return sdu;
}

/// Check two buffers are identical
static bool buffers_equal(const ByteBuffer& a, const ByteBuffer& b) {
    if (a.size() != b.size()) return false;
    return std::memcmp(a.data.data(), b.data.data(), a.size()) == 0;
}

// ---- Test: basic round-trip with 12-bit SN, ciphering + integrity ON ----
static void test_basic_roundtrip_12bit() {
    TEST("Basic round-trip (12-bit SN, cipher+integrity ON)");

    Config cfg;
    cfg.pdcp_sn_length    = 12;
    cfg.ciphering_enabled = true;
    cfg.integrity_enabled = true;

    PdcpLayer pdcp(cfg);
    ByteBuffer sdu = make_test_sdu(100, 0xAA);

    ByteBuffer pdu = pdcp.process_tx(sdu);

    // PDU should be larger than SDU: 2-byte header + 4-byte MAC-I
    if (pdu.size() != sdu.size() + 2 + 4) {
        FAIL("Unexpected PDU size: " + std::to_string(pdu.size()));
        return;
    }

    // Reset RX counter to match TX (both start at 0, so just create a new instance)
    PdcpLayer pdcp_rx(cfg);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: basic round-trip with 18-bit SN ----
static void test_basic_roundtrip_18bit() {
    TEST("Basic round-trip (18-bit SN)");

    Config cfg;
    cfg.pdcp_sn_length    = 18;
    cfg.ciphering_enabled = true;
    cfg.integrity_enabled = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(200, 0x55);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);

    // 18-bit SN: 3-byte header + 4-byte MAC-I
    if (pdu.size() != sdu.size() + 3 + 4) {
        FAIL("Unexpected PDU size: " + std::to_string(pdu.size()));
        return;
    }

    ByteBuffer recovered = pdcp_rx.process_rx(pdu);
    if (!buffers_equal(sdu, recovered)) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: multiple sequential packets ----
static void test_multiple_packets() {
    TEST("Multiple sequential packets (10 packets)");

    Config cfg;
    cfg.pdcp_sn_length    = 12;
    cfg.ciphering_enabled = true;
    cfg.integrity_enabled = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    for (int i = 0; i < 10; i++) {
        ByteBuffer sdu = make_test_sdu(150, static_cast<uint8_t>(i));
        ByteBuffer pdu = pdcp_tx.process_tx(sdu);
        ByteBuffer recovered = pdcp_rx.process_rx(pdu);
        if (!buffers_equal(sdu, recovered)) {
            FAIL("Mismatch on packet " + std::to_string(i));
            return;
        }
    }

    PASS();
}

// ---- Test: ciphering disabled, integrity enabled ----
static void test_no_ciphering() {
    TEST("Ciphering disabled, integrity enabled");

    Config cfg;
    cfg.pdcp_sn_length    = 12;
    cfg.ciphering_enabled = false;
    cfg.integrity_enabled = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(80, 0x33);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: both ciphering and integrity disabled ----
static void test_no_ciphering_no_integrity() {
    TEST("Both ciphering and integrity disabled");

    Config cfg;
    cfg.pdcp_sn_length    = 12;
    cfg.ciphering_enabled = false;
    cfg.integrity_enabled = false;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(60, 0x77);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);

    // No MAC-I: PDU = 2-byte header + payload
    if (pdu.size() != sdu.size() + 2) {
        FAIL("Unexpected PDU size without integrity: " + std::to_string(pdu.size()));
        return;
    }

    ByteBuffer recovered = pdcp_rx.process_rx(pdu);
    if (!buffers_equal(sdu, recovered)) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: large payload (1400 bytes, typical IP packet) ----
static void test_large_payload() {
    TEST("Large payload (1400 bytes)");

    Config cfg;
    cfg.pdcp_sn_length    = 12;
    cfg.ciphering_enabled = true;
    cfg.integrity_enabled = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(1400, 0xBB);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ================================================================
// NEW TESTS — Member 1: AES-128-CTR + HMAC-SHA256
// ================================================================

// ---- Test: AES-128-CTR round-trip (12-bit SN) ----
static void test_aes_roundtrip_12bit() {
    TEST("AES-128-CTR round-trip (12-bit SN)");

    Config cfg;
    cfg.pdcp_sn_length      = 12;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;
    cfg.cipher_algorithm    = 1;  // AES-128-CTR
    cfg.integrity_algorithm = 0;  // keep CRC32 to isolate cipher test

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(1400, 0xAA);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) { FAIL("Recovered SDU mismatch"); return; }
    PASS();
}

// ---- Test: HMAC-SHA256 round-trip (12-bit SN) ----
static void test_hmac_roundtrip_12bit() {
    TEST("HMAC-SHA256 round-trip (12-bit SN)");

    Config cfg;
    cfg.pdcp_sn_length      = 12;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;
    cfg.cipher_algorithm    = 0;  // keep XOR to isolate integrity test
    cfg.integrity_algorithm = 1;  // HMAC-SHA256

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(1400, 0xBB);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) { FAIL("Recovered SDU mismatch"); return; }
    PASS();
}

// ---- Test: AES + HMAC combined round-trip ----
static void test_aes_hmac_combined() {
    TEST("AES-128-CTR + HMAC-SHA256 combined round-trip");

    Config cfg;
    cfg.pdcp_sn_length      = 12;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;
    cfg.cipher_algorithm    = 1;
    cfg.integrity_algorithm = 1;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(1400, 0xCC);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) { FAIL("Recovered SDU mismatch"); return; }
    PASS();
}

// ---- Test: AES ciphertext differs from plaintext ----
static void test_aes_actually_encrypts() {
    TEST("AES ciphertext differs from plaintext");

    Config cfg;
    cfg.pdcp_sn_length      = 12;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = false;  // disable to simplify PDU structure
    cfg.cipher_algorithm    = 1;

    PdcpLayer pdcp_tx(cfg);
    ByteBuffer sdu = make_test_sdu(100, 0xDD);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);

    // Extract the ciphered payload (skip 2-byte header, no MAC-I)
    std::vector<uint8_t> ciphered(pdu.data.begin() + 2, pdu.data.end());

    // It must NOT match the original payload
    if (ciphered.size() == sdu.data.size() &&
        std::memcmp(ciphered.data(), sdu.data.data(), sdu.data.size()) == 0) {
        FAIL("Ciphertext is identical to plaintext — encryption not working");
        return;
    }
    PASS();
}

// ---- Test: Wrong integrity key causes failure ----
static void test_hmac_wrong_key_fails() {
    TEST("HMAC wrong key detected");

    Config cfg_tx;
    cfg_tx.pdcp_sn_length      = 12;
    cfg_tx.ciphering_enabled   = true;
    cfg_tx.integrity_enabled   = true;
    cfg_tx.cipher_algorithm    = 1;
    cfg_tx.integrity_algorithm = 1;

    // RX config with a DIFFERENT integrity key
    Config cfg_rx = cfg_tx;
    std::memset(cfg_rx.integrity_key, 0xFF, 16);  // all 0xFF — differs from TX key

    PdcpLayer pdcp_tx(cfg_tx);
    PdcpLayer pdcp_rx(cfg_rx);

    ByteBuffer sdu = make_test_sdu(200, 0xEE);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    // Should return empty buffer (integrity check failed)
    if (!recovered.empty()) {
        FAIL("Integrity check should have failed with wrong key");
        return;
    }
    PASS();
}

// ---- Test: XOR vs AES produce different ciphertext ----
static void test_xor_vs_aes_differ() {
    TEST("XOR and AES produce different ciphertext");

    Config cfg_xor;
    cfg_xor.pdcp_sn_length      = 12;
    cfg_xor.ciphering_enabled   = true;
    cfg_xor.integrity_enabled   = false;
    cfg_xor.cipher_algorithm    = 0;  // XOR

    Config cfg_aes = cfg_xor;
    cfg_aes.cipher_algorithm    = 1;  // AES

    PdcpLayer pdcp_xor(cfg_xor);
    PdcpLayer pdcp_aes(cfg_aes);

    ByteBuffer sdu = make_test_sdu(100, 0x42);
    ByteBuffer pdu_xor = pdcp_xor.process_tx(sdu);
    ByteBuffer pdu_aes = pdcp_aes.process_tx(sdu);

    // Both round-trip correctly
    PdcpLayer rx_xor(cfg_xor);
    PdcpLayer rx_aes(cfg_aes);

    ByteBuffer rec_xor = rx_xor.process_rx(pdu_xor);
    ByteBuffer rec_aes = rx_aes.process_rx(pdu_aes);

    if (!buffers_equal(sdu, rec_xor) || !buffers_equal(sdu, rec_aes)) {
        FAIL("One or both round-trips failed");
        return;
    }

    // The ciphered PDUs should be different (different algorithms, same input)
    if (buffers_equal(pdu_xor, pdu_aes)) {
        FAIL("XOR and AES produced identical output");
        return;
    }
    PASS();
}

// ---- Test: 18-bit SN with AES + HMAC ----
static void test_aes_hmac_18bit() {
    TEST("AES + HMAC with 18-bit SN");

    Config cfg;
    cfg.pdcp_sn_length      = 18;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;
    cfg.cipher_algorithm    = 1;
    cfg.integrity_algorithm = 1;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(500, 0x77);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);

    // 18-bit SN: 3-byte header + payload + 4-byte MAC-I
    if (pdu.size() != sdu.size() + 3 + 4) {
        FAIL("Unexpected PDU size: " + std::to_string(pdu.size()));
        return;
    }

    ByteBuffer recovered = pdcp_rx.process_rx(pdu);
    if (!buffers_equal(sdu, recovered)) { FAIL("Recovered SDU mismatch"); return; }
    PASS();
}

// ---- Test: Large payload 9000 bytes (max PDCP SDU per TS 38.323) ----
static void test_aes_hmac_max_sdu() {
    TEST("AES + HMAC with 9000-byte payload (max PDCP SDU)");

    Config cfg;
    cfg.pdcp_sn_length      = 12;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;
    cfg.cipher_algorithm    = 1;
    cfg.integrity_algorithm = 1;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    ByteBuffer sdu = make_test_sdu(9000, 0x99);
    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) { FAIL("Recovered SDU mismatch"); return; }
    PASS();
}

// ---- Test: Multiple sequential packets with AES + HMAC ----
static void test_aes_hmac_sequential() {
    TEST("AES + HMAC sequential packets (20 packets)");

    Config cfg;
    cfg.pdcp_sn_length      = 12;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;
    cfg.cipher_algorithm    = 1;
    cfg.integrity_algorithm = 1;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    for (int i = 0; i < 20; i++) {
        ByteBuffer sdu = make_test_sdu(200 + i * 50, static_cast<uint8_t>(i));
        ByteBuffer pdu = pdcp_tx.process_tx(sdu);
        ByteBuffer recovered = pdcp_rx.process_rx(pdu);
        if (!buffers_equal(sdu, recovered)) {
            FAIL("Mismatch on packet " + std::to_string(i));
            return;
        }
    }
    PASS();
}


// ============================================================
// Member 2: Generate a valid IPv4 packet for compression testing
// ============================================================
static ByteBuffer make_ipv4_packet(size_t total_size, uint16_t identification = 0x0001) {
    ByteBuffer pkt;
    pkt.data.resize(total_size);

    // IPv4 header (20 bytes)
    pkt.data[0] = 0x45;  // Version=4, IHL=5
    pkt.data[1] = 0x00;  // DSCP + ECN
    pkt.data[2] = static_cast<uint8_t>((total_size >> 8) & 0xFF);  // Total Length hi
    pkt.data[3] = static_cast<uint8_t>(total_size & 0xFF);          // Total Length lo
    pkt.data[4] = static_cast<uint8_t>((identification >> 8) & 0xFF);  // Identification hi
    pkt.data[5] = static_cast<uint8_t>(identification & 0xFF);          // Identification lo
    pkt.data[6] = 0x40;  // Flags: Don't Fragment
    pkt.data[7] = 0x00;  // Fragment Offset
    pkt.data[8] = 0x40;  // TTL = 64
    pkt.data[9] = 0x11;  // Protocol = UDP (17)
    pkt.data[10] = 0x00; // Header Checksum hi
    pkt.data[11] = 0x00; // Header Checksum lo
    pkt.data[12] = 10;   // Src IP: 10.0.0.1
    pkt.data[13] = 0;
    pkt.data[14] = 0;
    pkt.data[15] = 1;
    pkt.data[16] = 10;   // Dst IP: 10.0.0.2
    pkt.data[17] = 0;
    pkt.data[18] = 0;
    pkt.data[19] = 2;

    // Fill payload after IP header with recognizable pattern
    for (size_t i = 20; i < total_size; i++) {
        pkt.data[i] = static_cast<uint8_t>((identification + i) & 0xFF);
    }

    return pkt;
}
// ============================================================
// Member 2 Test:
// Verifies compression + decompression correctness
//
// Ensures:
//   TX → compress → cipher → RX → decipher → decompress
//   results in original SDU
// ============================================================
static void test_compression_roundtrip() {
    TEST("Compression round-trip (10 packets)");

    Config cfg;
    cfg.compression_enabled = true;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    for (int i = 0; i < 10; i++) {
        ByteBuffer sdu = make_ipv4_packet(500, static_cast<uint16_t>(i + 1));
        ByteBuffer pdu = pdcp_tx.process_tx(sdu);
        ByteBuffer recovered = pdcp_rx.process_rx(pdu);

        if (!buffers_equal(sdu, recovered)) {
            FAIL("Mismatch on packet " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ============================================================
// Member 2 Test:
// Verifies compression reduces packet size
//
// Expected:
//   compressed PDU < original SDU
// ============================================================
static void test_compression_reduces_size() {
    TEST("Second packet is smaller (13 bytes saved)");

    Config cfg;
    cfg.compression_enabled = true;
    cfg.ciphering_enabled   = false;
    cfg.integrity_enabled   = false;

    PdcpLayer pdcp_tx(cfg);

    // First packet — establishes context, sent uncompressed
    ByteBuffer sdu1 = make_ipv4_packet(200, 0x0001);
    ByteBuffer pdu1 = pdcp_tx.process_tx(sdu1);

    // Second packet — should be compressed (13 bytes smaller)
    ByteBuffer sdu2 = make_ipv4_packet(200, 0x0002);
    ByteBuffer pdu2 = pdcp_tx.process_tx(sdu2);

    // Expected: pdu2 is 13 bytes smaller than pdu1
    // pdu1 = 2 (PDCP header) + 200 (uncompressed) = 202
    // pdu2 = 2 (PDCP header) + 187 (compressed: 200 - 13) = 189
    size_t expected_saving = 13;  // 20-byte IP header → 7-byte compressed header
    if (pdu1.size() - pdu2.size() != expected_saving) {
        FAIL("Expected " + std::to_string(expected_saving) + " bytes saved, got " +
             std::to_string(pdu1.size()) + " - " + std::to_string(pdu2.size()) + " = " +
             std::to_string(pdu1.size() - pdu2.size()));
        return;
    }
    PASS();
}

// ============================================================
// Member 2 Test:
// Ensures disabling compression keeps behavior unchanged
// ============================================================
static void test_compression_disabled() {
    TEST("Compression disabled");

    Config cfg;
    cfg.compression_enabled = false;

    PdcpLayer tx(cfg);
    PdcpLayer rx(cfg);

    ByteBuffer sdu = make_test_sdu(100, 0xAA);
    ByteBuffer pdu = tx.process_tx(sdu);
    ByteBuffer rec = rx.process_rx(pdu);

    if (!buffers_equal(sdu, rec)) {
        FAIL("Disabled compression altered data");
        return;
    }

    PASS();
}

// ---- Test: First packet is uncompressed (context establishment) ----
static void test_compression_first_packet_uncompressed() {
    TEST("First packet sent uncompressed (context establishment)");

    Config cfg_comp;
    cfg_comp.compression_enabled = true;
    cfg_comp.ciphering_enabled   = false;
    cfg_comp.integrity_enabled   = false;

    Config cfg_nocomp;
    cfg_nocomp.compression_enabled = false;
    cfg_nocomp.ciphering_enabled   = false;
    cfg_nocomp.integrity_enabled   = false;

    PdcpLayer pdcp_comp(cfg_comp);
    PdcpLayer pdcp_nocomp(cfg_nocomp);

    ByteBuffer sdu = make_ipv4_packet(200, 0x0001);
    ByteBuffer pdu_comp   = pdcp_comp.process_tx(sdu);
    ByteBuffer pdu_nocomp = pdcp_nocomp.process_tx(sdu);

    // First packet should be same size (uncompressed, context not yet established)
    if (pdu_comp.size() != pdu_nocomp.size()) {
        FAIL("First packet should be uncompressed but sizes differ: " +
             std::to_string(pdu_comp.size()) + " vs " + std::to_string(pdu_nocomp.size()));
        return;
    }
    PASS();
}

// ---- Test: Compression + AES-128-CTR + HMAC-SHA256 combined ----
static void test_compression_with_aes_hmac() {
    TEST("Compression + AES-128-CTR + HMAC-SHA256 combined");

    Config cfg;
    cfg.compression_enabled   = true;
    cfg.ciphering_enabled     = true;
    cfg.integrity_enabled     = true;
    cfg.cipher_algorithm      = 1;  // AES-128-CTR
    cfg.integrity_algorithm   = 1;  // HMAC-SHA256

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    for (int i = 0; i < 10; i++) {
        ByteBuffer sdu = make_ipv4_packet(1400, static_cast<uint16_t>(i + 1));
        ByteBuffer pdu = pdcp_tx.process_tx(sdu);
        ByteBuffer recovered = pdcp_rx.process_rx(pdu);

        if (!buffers_equal(sdu, recovered)) {
            FAIL("Mismatch on packet " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ---- Test: Compression with 18-bit SN ----
static void test_compression_18bit_sn() {
    TEST("Compression with 18-bit SN");

    Config cfg;
    cfg.pdcp_sn_length        = 18;
    cfg.compression_enabled   = true;
    cfg.ciphering_enabled     = true;
    cfg.integrity_enabled     = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    for (int i = 0; i < 5; i++) {
        ByteBuffer sdu = make_ipv4_packet(800, static_cast<uint16_t>(i + 100));
        ByteBuffer pdu = pdcp_tx.process_tx(sdu);
        ByteBuffer recovered = pdcp_rx.process_rx(pdu);

        if (!buffers_equal(sdu, recovered)) {
            FAIL("Mismatch on packet " + std::to_string(i));
            return;
        }
    }
    PASS();
}

// ---- Test: Non-IPv4 packet passes through compression unmodified ----
static void test_compression_non_ipv4_passthrough() {
    TEST("Non-IPv4 packet passes through unmodified");

    Config cfg;
    cfg.compression_enabled = true;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    // Create a packet that does NOT start with 0x45
    ByteBuffer sdu = make_test_sdu(200, 0xBB);

    ByteBuffer pdu = pdcp_tx.process_tx(sdu);
    ByteBuffer recovered = pdcp_rx.process_rx(pdu);

    if (!buffers_equal(sdu, recovered)) {
        FAIL("Non-IPv4 packet round-trip failed");
        return;
    }
    PASS();
}

// ---- Test: Large packet compression (9000 bytes) ----
static void test_compression_large_packet() {
    TEST("Compression with 9000-byte packet");

    Config cfg;
    cfg.compression_enabled = true;
    cfg.ciphering_enabled   = true;
    cfg.integrity_enabled   = true;

    PdcpLayer pdcp_tx(cfg);
    PdcpLayer pdcp_rx(cfg);

    // First packet establishes context
    ByteBuffer sdu1 = make_ipv4_packet(9000, 0x0001);
    ByteBuffer pdu1 = pdcp_tx.process_tx(sdu1);
    ByteBuffer rec1 = pdcp_rx.process_rx(pdu1);
    if (!buffers_equal(sdu1, rec1)) { FAIL("First packet round-trip failed"); return; }

    // Second packet uses compression
    ByteBuffer sdu2 = make_ipv4_packet(9000, 0x0002);
    ByteBuffer pdu2 = pdcp_tx.process_tx(sdu2);
    ByteBuffer rec2 = pdcp_rx.process_rx(pdu2);
    if (!buffers_equal(sdu2, rec2)) { FAIL("Second packet round-trip failed"); return; }

    PASS();
}

static void profile_compression() {
    const int ITERATIONS = 1000;
    const std::vector<uint32_t> pkt_sizes = {100, 500, 1000, 1400, 3000, 9000};

    std::cout << "\n==========================================\n";
    std::cout << "  PDCP Profiling: Compression\n";
    std::cout << "==========================================\n\n";

    std::cout << std::left
              << std::setw(10) << "PktSize"
              << std::setw(15) << "Compression"
              << std::setw(14) << "TX avg(us)"
              << std::setw(14) << "RX avg(us)"
              << std::setw(14) << "PDU Size"
              << std::setw(14) << "Saved(B)"
              << std::endl;
    std::cout << std::string(81, '-') << std::endl;

    for (uint32_t pkt_size : pkt_sizes) {
        size_t nocomp_pdu_size = 0;

        for (bool comp : {false, true}) {
            Config cfg;
            cfg.ciphering_enabled   = true;
            cfg.integrity_enabled   = true;
            cfg.compression_enabled = comp;

            PdcpLayer pdcp_tx(cfg);
            PdcpLayer pdcp_rx(cfg);

            // First packet to establish context (not measured)
            if (comp) {
                ByteBuffer setup = make_ipv4_packet(pkt_size, 0x0000);
                auto setup_pdu = pdcp_tx.process_tx(setup);
                pdcp_rx.process_rx(setup_pdu);
            }

            double total_tx = 0, total_rx = 0;
            size_t pdu_size = 0;

            for (int i = 0; i < ITERATIONS; i++) {
                ByteBuffer sdu = make_ipv4_packet(pkt_size, static_cast<uint16_t>(i + 1));

                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer pdu = pdcp_tx.process_tx(sdu);
                auto t1 = std::chrono::high_resolution_clock::now();
                ByteBuffer rec = pdcp_rx.process_rx(pdu);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx += std::chrono::duration<double, std::micro>(t2 - t1).count();
                if (i == 0) pdu_size = pdu.size();
            }

            if (!comp) nocomp_pdu_size = pdu_size;

            int saved = comp ? static_cast<int>(nocomp_pdu_size) - static_cast<int>(pdu_size) : 0;

            std::cout << std::left
                      << std::setw(10) << pkt_size
                      << std::setw(15) << (comp ? "ON" : "OFF")
                      << std::setw(14) << std::fixed << std::setprecision(2) << (total_tx / ITERATIONS)
                      << std::setw(14) << (total_rx / ITERATIONS)
                      << std::setw(14) << pdu_size
                      << std::setw(14) << (comp ? std::to_string(saved) : "-")
                      << std::endl;
        }
    }
}

static void profile_pdcp_variants() {
    const int ITERATIONS = 1000;
    const std::vector<uint32_t> pkt_sizes = {100, 500, 1000, 1400, 3000, 9000};

    std::cout << "\n==========================================\n";
    std::cout << "  PDCP Profiling: Cipher Variants\n";
    std::cout << "==========================================\n\n";

    std::cout << std::left
              << std::setw(10) << "PktSize"
              << std::setw(15) << "Cipher"
              << std::setw(14) << "TX avg(us)"
              << std::setw(14) << "RX avg(us)"
              << std::endl;
    std::cout << std::string(53, '-') << std::endl;

    for (uint32_t pkt_size : pkt_sizes) {
        for (uint8_t algo : {0, 1}) {
            Config cfg;
            cfg.pdcp_sn_length      = 12;
            cfg.ciphering_enabled   = true;
            cfg.integrity_enabled   = true;
            cfg.cipher_algorithm    = algo;
            cfg.integrity_algorithm = 0;  // hold integrity constant

            PdcpLayer pdcp_tx(cfg);
            PdcpLayer pdcp_rx(cfg);

            ByteBuffer sdu = make_test_sdu(pkt_size, 0xAB);

            double total_tx = 0, total_rx = 0;
            for (int i = 0; i < ITERATIONS; i++) {
                pdcp_tx.reset();
                pdcp_rx.reset();

                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer pdu = pdcp_tx.process_tx(sdu);
                auto t1 = std::chrono::high_resolution_clock::now();
                ByteBuffer rec = pdcp_rx.process_rx(pdu);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx += std::chrono::duration<double, std::micro>(t2 - t1).count();
            }

            std::cout << std::left
                      << std::setw(10) << pkt_size
                      << std::setw(15) << (algo == 0 ? "XOR" : "AES-128-CTR")
                      << std::setw(14) << std::fixed << std::setprecision(2) << (total_tx / ITERATIONS)
                      << std::setw(14) << (total_rx / ITERATIONS)
                      << std::endl;
        }
    }

    std::cout << "\n==========================================\n";
    std::cout << "  PDCP Profiling: Integrity Variants\n";
    std::cout << "==========================================\n\n";

    std::cout << std::left
              << std::setw(10) << "PktSize"
              << std::setw(15) << "Integrity"
              << std::setw(14) << "TX avg(us)"
              << std::setw(14) << "RX avg(us)"
              << std::endl;
    std::cout << std::string(53, '-') << std::endl;

    for (uint32_t pkt_size : pkt_sizes) {
        for (uint8_t algo : {0, 1}) {
            Config cfg;
            cfg.pdcp_sn_length      = 12;
            cfg.ciphering_enabled   = true;
            cfg.integrity_enabled   = true;
            cfg.cipher_algorithm    = 0;  // hold cipher constant
            cfg.integrity_algorithm = algo;

            PdcpLayer pdcp_tx(cfg);
            PdcpLayer pdcp_rx(cfg);

            ByteBuffer sdu = make_test_sdu(pkt_size, 0xCD);

            double total_tx = 0, total_rx = 0;
            for (int i = 0; i < ITERATIONS; i++) {
                pdcp_tx.reset();
                pdcp_rx.reset();

                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer pdu = pdcp_tx.process_tx(sdu);
                auto t1 = std::chrono::high_resolution_clock::now();
                ByteBuffer rec = pdcp_rx.process_rx(pdu);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx += std::chrono::duration<double, std::micro>(t2 - t1).count();
            }

            std::cout << std::left
                      << std::setw(10) << pkt_size
                      << std::setw(15) << (algo == 0 ? "CRC32" : "HMAC-SHA256")
                      << std::setw(14) << std::fixed << std::setprecision(2) << (total_tx / ITERATIONS)
                      << std::setw(14) << (total_rx / ITERATIONS)
                      << std::endl;
        }
    }
}

int main() {
    std::cout << "==============================\n";
    std::cout << " PDCP Layer Unit Tests\n";
    std::cout << "==============================\n";

    test_basic_roundtrip_12bit();
    test_basic_roundtrip_18bit();
    test_multiple_packets();
    test_no_ciphering();
    test_no_ciphering_no_integrity();
    test_large_payload();

    // --- NEW: Member 1 AES/HMAC tests ---
    std::cout << "\n  --- AES-128-CTR / HMAC-SHA256 Tests ---\n";
    test_aes_roundtrip_12bit();
    test_hmac_roundtrip_12bit();
    test_aes_hmac_combined();
    test_aes_actually_encrypts();
    test_hmac_wrong_key_fails();
    test_xor_vs_aes_differ();
    test_aes_hmac_18bit();
    test_aes_hmac_max_sdu();
    test_aes_hmac_sequential();
    // Run profiling (after all correctness tests pass)
    profile_pdcp_variants();
    // ============================================================
    // Member 2 Test Suite
    // ============================================================
    std::cout << "\n  --- Compression Tests ---\n";
    test_compression_roundtrip();
    test_compression_first_packet_uncompressed();
    test_compression_reduces_size();
    test_compression_disabled();
    test_compression_with_aes_hmac();
    test_compression_18bit_sn();
    test_compression_non_ipv4_passthrough();
    test_compression_large_packet();
    profile_compression();
    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
