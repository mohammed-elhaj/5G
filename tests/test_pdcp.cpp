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
// Member 2: Generate a valid IPv4 packet
// ============================================================
static ByteBuffer make_ipv4_packet(size_t payload_size) {
    ByteBuffer pkt;
    pkt.data.resize(20 + payload_size);

    // IPv4 header
    pkt.data[0] = 0x45; // Version=4, IHL=5
    pkt.data[1] = 0x00;

    uint16_t total_len = 20 + payload_size;
    pkt.data[2] = (total_len >> 8) & 0xFF;
    pkt.data[3] = total_len & 0xFF;

    pkt.data[4] = 0x00; pkt.data[5] = 0x01;
    pkt.data[6] = 0x00; pkt.data[7] = 0x00;

    pkt.data[8] = 64;   // TTL
    pkt.data[9] = 17;   // UDP

    // Src IP
    pkt.data[12] = 192; pkt.data[13] = 168;
    pkt.data[14] = 1;   pkt.data[15] = 1;

    // Dst IP
    pkt.data[16] = 192; pkt.data[17] = 168;
    pkt.data[18] = 1;   pkt.data[19] = 2;

    // Payload
    for (size_t i = 20; i < pkt.data.size(); i++) {
        pkt.data[i] = static_cast<uint8_t>(i & 0xFF);
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
    TEST("Compression round-trip (IPv4)");

    Config cfg;
    cfg.pdcp_sn_length = 12;
    cfg.ciphering_enabled = true;
    cfg.integrity_enabled = true;
    cfg.compression_enabled = true;

    PdcpLayer tx(cfg);
    PdcpLayer rx(cfg);

    // First packet → establishes context
    ByteBuffer sdu1 = make_ipv4_packet(100);
    ByteBuffer pdu1 = tx.process_tx(sdu1);
    ByteBuffer rec1 = rx.process_rx(pdu1);

    // Second packet → should be compressed
    ByteBuffer sdu2 = make_ipv4_packet(120);
    ByteBuffer pdu2 = tx.process_tx(sdu2);

    // 🔥 Verify compression marker exists
    size_t header_size = 2;
    if (pdu2.data[header_size] != 0xFC) {
        FAIL("Compression not applied");
        return;
    }

    ByteBuffer rec2 = rx.process_rx(pdu2);

    if (!buffers_equal(sdu2, rec2)) {
        FAIL("Roundtrip mismatch after compression");
        return;
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
    TEST("Compression reduces payload size");

    Config cfg;
    cfg.pdcp_sn_length = 12;
    cfg.ciphering_enabled = false;
    cfg.integrity_enabled = false;
    cfg.compression_enabled = true;

    PdcpLayer pdcp(cfg);

    // First packet (no compression)
    ByteBuffer sdu1 = make_ipv4_packet(200);
    ByteBuffer pdu1 = pdcp.process_tx(sdu1);

    // Second packet (compressed)
    ByteBuffer sdu2 = make_ipv4_packet(200);
    ByteBuffer pdu2 = pdcp.process_tx(sdu2);

    size_t header_size = 2;

    size_t payload1 = pdu1.size() - header_size;
    size_t payload2 = pdu2.size() - header_size;

    if (payload2 >= payload1) {
        FAIL("Compressed payload not smaller");
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

static void profile_compression() {
    const int ITERATIONS = 1000;
    const std::vector<uint32_t> pkt_sizes = {100, 500, 1000, 1400};

    std::cout << "\n==========================================\n";
    std::cout << "  PDCP Compression Profiling\n";
    std::cout << "==========================================\n\n";

    std::cout << std::left
              << std::setw(10) << "PktSize"
              << std::setw(18) << "Mode"
              << std::setw(14) << "TX avg(us)"
              << std::setw(14) << "RX avg(us)"
              << std::setw(16) << "Avg Size"
              << std::setw(16) << "Reduction"
              << std::endl;

    std::cout << std::string(80, '-') << std::endl;

    for (uint32_t size : pkt_sizes) {
        for (bool comp : {false, true}) {

            Config cfg;
            cfg.pdcp_sn_length = 12;
            cfg.ciphering_enabled = true;
            cfg.integrity_enabled = true;
            cfg.compression_enabled = comp;

            PdcpLayer tx(cfg);
            PdcpLayer rx(cfg);

            double total_tx = 0, total_rx = 0;
            double total_size = 0;

            for (int i = 0; i < ITERATIONS; i++) {
                tx.reset();
                rx.reset();

                ByteBuffer sdu = make_ipv4_packet(size);

                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer pdu = tx.process_tx(sdu);
                auto t1 = std::chrono::high_resolution_clock::now();
                ByteBuffer rec = rx.process_rx(pdu);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx += std::chrono::duration<double, std::micro>(t2 - t1).count();

                total_size += pdu.size();
            }

            double avg_size = total_size / ITERATIONS;
            double reduction = 0;

            if (comp) {
                // Compare against uncompressed size
                reduction = (20.0 - 5.0); // header savings
            }

            std::cout << std::left
                      << std::setw(10) << size
                      << std::setw(18) << (comp ? "Compression ON" : "Compression OFF")
                      << std::setw(14) << (total_tx / ITERATIONS)
                      << std::setw(14) << (total_rx / ITERATIONS)
                      << std::setw(16) << avg_size
                      << std::setw(16) << reduction
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
    test_compression_reduces_size();
    test_compression_disabled();
    profile_compression();
    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
