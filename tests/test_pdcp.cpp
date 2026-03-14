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

    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
