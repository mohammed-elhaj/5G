// ============================================================
// test_rlc.cpp — Unit test for the RLC layer (UM mode)
//
// Tests the RLC TX (segmentation) → RX (reassembly) round-trip:
//   1. Create an SDU
//   2. Segment it via process_tx → get one or more RLC PDUs
//   3. Feed each PDU into process_rx
//   4. Verify the reassembled SDU matches the original
//
// Also tests:
//   - Small SDU (no segmentation needed)
//   - SDU that requires exactly 2 segments
//   - Large SDU requiring many segments
//   - Multiple sequential SDUs
// ============================================================

#include "rlc.h"
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

static ByteBuffer make_test_sdu(size_t size, uint8_t seed) {
    ByteBuffer sdu;
    sdu.data.resize(size);
    for (size_t i = 0; i < size; i++) {
        sdu.data[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    }
    return sdu;
}

static bool buffers_equal(const ByteBuffer& a, const ByteBuffer& b) {
    if (a.size() != b.size()) return false;
    return std::memcmp(a.data.data(), b.data.data(), a.size()) == 0;
}

// ---- Test: small SDU that fits in a single PDU (no segmentation) ----
static void test_no_segmentation() {
    TEST("Small SDU (no segmentation, SI=00)");

    Config cfg;
    cfg.rlc_sn_length    = 6;
    cfg.rlc_max_pdu_size = 500;

    RlcLayer rlc_tx(cfg);
    RlcLayer rlc_rx(cfg);

    ByteBuffer sdu = make_test_sdu(100, 0x10);   // 100 + 1-byte header = 101 < 500

    auto pdus = rlc_tx.process_tx(sdu);
    if (pdus.size() != 1) {
        FAIL("Expected 1 PDU, got " + std::to_string(pdus.size()));
        return;
    }

    // Feed into RX
    std::vector<ByteBuffer> result;
    for (auto& p : pdus) {
        auto reassembled = rlc_rx.process_rx(p);
        result.insert(result.end(), reassembled.begin(), reassembled.end());
    }

    if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
        FAIL("Reassembled SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: SDU requires segmentation into 2 segments ----
static void test_two_segments() {
    TEST("SDU segmented into 2 pieces");

    Config cfg;
    cfg.rlc_sn_length    = 6;
    cfg.rlc_max_pdu_size = 300;   // first segment: 299 data bytes, rest goes to second

    RlcLayer rlc_tx(cfg);
    RlcLayer rlc_rx(cfg);

    ByteBuffer sdu = make_test_sdu(500, 0x20);

    auto pdus = rlc_tx.process_tx(sdu);
    if (pdus.size() < 2) {
        FAIL("Expected at least 2 PDUs, got " + std::to_string(pdus.size()));
        return;
    }

    std::vector<ByteBuffer> result;
    for (auto& p : pdus) {
        auto reassembled = rlc_rx.process_rx(p);
        result.insert(result.end(), reassembled.begin(), reassembled.end());
    }

    if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
        FAIL("Reassembled SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: large SDU requiring many segments ----
static void test_many_segments() {
    TEST("Large SDU (1400 bytes, many segments)");

    Config cfg;
    cfg.rlc_sn_length    = 6;
    cfg.rlc_max_pdu_size = 200;

    RlcLayer rlc_tx(cfg);
    RlcLayer rlc_rx(cfg);

    ByteBuffer sdu = make_test_sdu(1400, 0x30);

    auto pdus = rlc_tx.process_tx(sdu);
    std::cout << "(" << pdus.size() << " segments) ";

    std::vector<ByteBuffer> result;
    for (auto& p : pdus) {
        auto reassembled = rlc_rx.process_rx(p);
        result.insert(result.end(), reassembled.begin(), reassembled.end());
    }

    if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
        FAIL("Reassembled SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: multiple sequential SDUs ----
static void test_multiple_sdus() {
    TEST("Multiple sequential SDUs (5 packets)");

    Config cfg;
    cfg.rlc_sn_length    = 6;
    cfg.rlc_max_pdu_size = 300;

    RlcLayer rlc_tx(cfg);
    RlcLayer rlc_rx(cfg);

    for (int i = 0; i < 5; i++) {
        ByteBuffer sdu = make_test_sdu(400 + i * 50, static_cast<uint8_t>(i));
        auto pdus = rlc_tx.process_tx(sdu);

        std::vector<ByteBuffer> result;
        for (auto& p : pdus) {
            auto reassembled = rlc_rx.process_rx(p);
            result.insert(result.end(), reassembled.begin(), reassembled.end());
        }

        if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
            FAIL("Mismatch on SDU " + std::to_string(i));
            return;
        }
    }

    PASS();
}

// ---- Test: SDU that exactly fills one PDU (boundary case) ----
static void test_exact_fit() {
    TEST("SDU exactly fits one PDU (boundary case)");

    Config cfg;
    cfg.rlc_sn_length    = 6;
    cfg.rlc_max_pdu_size = 501;   // 1-byte header + 500-byte data = 501

    RlcLayer rlc_tx(cfg);
    RlcLayer rlc_rx(cfg);

    ByteBuffer sdu = make_test_sdu(500, 0x44);

    auto pdus = rlc_tx.process_tx(sdu);
    if (pdus.size() != 1) {
        FAIL("Expected 1 PDU, got " + std::to_string(pdus.size()));
        return;
    }

    std::vector<ByteBuffer> result;
    for (auto& p : pdus) {
        auto reassembled = rlc_rx.process_rx(p);
        result.insert(result.end(), reassembled.begin(), reassembled.end());
    }

    if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
        FAIL("Reassembled SDU does not match original");
        return;
    }

    PASS();
}

int main() {
    std::cout << "==============================\n";
    std::cout << " RLC Layer Unit Tests\n";
    std::cout << "==============================\n";

    test_no_segmentation();
    test_two_segments();
    test_many_segments();
    test_multiple_sdus();
    test_exact_fit();

    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
