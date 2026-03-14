// ============================================================
// test_mac.cpp — Unit test for the MAC layer
//
// Tests the MAC TX (multiplexing) → RX (demultiplexing) round-trip:
//   1. Create one or more MAC SDUs
//   2. Multiplex them into a Transport Block via process_tx
//   3. Demultiplex via process_rx
//   4. Verify recovered SDUs match the originals
//
// Also tests:
//   - Single small SDU (8-bit length field)
//   - Single large SDU (16-bit length field, > 255 bytes)
//   - Multiple SDUs in one TB
//   - Padding is correctly handled
// ============================================================

#include "mac.h"
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

// ---- Test: single small SDU (uses 8-bit length) ----
static void test_single_small_sdu() {
    TEST("Single small SDU (100 bytes, 8-bit L)");

    Config cfg;
    cfg.transport_block_size = 1024;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);
    ByteBuffer sdu = make_test_sdu(100, 0xAA);

    ByteBuffer tb = mac.process_tx({sdu});

    // TB should be exactly transport_block_size
    if (tb.size() != cfg.transport_block_size) {
        FAIL("TB size is " + std::to_string(tb.size()) + ", expected " +
             std::to_string(cfg.transport_block_size));
        return;
    }

    auto recovered = mac.process_rx(tb);
    if (recovered.size() != 1) {
        FAIL("Expected 1 SDU, got " + std::to_string(recovered.size()));
        return;
    }

    if (!buffers_equal(sdu, recovered[0])) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: single large SDU (uses 16-bit length) ----
static void test_single_large_sdu() {
    TEST("Single large SDU (500 bytes, 16-bit L)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);
    ByteBuffer sdu = make_test_sdu(500, 0xBB);

    ByteBuffer tb = mac.process_tx({sdu});
    auto recovered = mac.process_rx(tb);

    if (recovered.size() != 1 || !buffers_equal(sdu, recovered[0])) {
        FAIL("Recovered SDU does not match original");
        return;
    }

    PASS();
}

// ---- Test: multiple SDUs multiplexed into one TB ----
static void test_multiple_sdus() {
    TEST("Multiple SDUs (3 SDUs in one TB)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);

    std::vector<ByteBuffer> sdus;
    sdus.push_back(make_test_sdu(100, 0x11));
    sdus.push_back(make_test_sdu(200, 0x22));
    sdus.push_back(make_test_sdu(300, 0x33));

    ByteBuffer tb = mac.process_tx(sdus);
    auto recovered = mac.process_rx(tb);

    if (recovered.size() != 3) {
        FAIL("Expected 3 SDUs, got " + std::to_string(recovered.size()));
        return;
    }

    for (size_t i = 0; i < 3; i++) {
        if (!buffers_equal(sdus[i], recovered[i])) {
            FAIL("SDU " + std::to_string(i) + " mismatch");
            return;
        }
    }

    PASS();
}

// ---- Test: mix of small and large SDUs ----
static void test_mixed_sizes() {
    TEST("Mixed SDU sizes (50, 300, 150 bytes)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);

    std::vector<ByteBuffer> sdus;
    sdus.push_back(make_test_sdu(50,  0xAA));   // 8-bit L
    sdus.push_back(make_test_sdu(300, 0xBB));   // 16-bit L
    sdus.push_back(make_test_sdu(150, 0xCC));   // 8-bit L

    ByteBuffer tb = mac.process_tx(sdus);
    auto recovered = mac.process_rx(tb);

    if (recovered.size() != 3) {
        FAIL("Expected 3 SDUs, got " + std::to_string(recovered.size()));
        return;
    }

    for (size_t i = 0; i < 3; i++) {
        if (!buffers_equal(sdus[i], recovered[i])) {
            FAIL("SDU " + std::to_string(i) + " mismatch");
            return;
        }
    }

    PASS();
}

// ---- Test: padding fills the rest of the TB ----
static void test_padding() {
    TEST("Padding fills remaining TB space");

    Config cfg;
    cfg.transport_block_size = 512;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);
    ByteBuffer sdu = make_test_sdu(50, 0xDD);

    ByteBuffer tb = mac.process_tx({sdu});

    // The TB must be exactly transport_block_size
    if (tb.size() != 512) {
        FAIL("TB size is " + std::to_string(tb.size()));
        return;
    }

    // Check padding LCID byte: after the SDU (2-byte subheader + 50 data = 52),
    // byte 52 should be LCID 63 (padding)
    if (tb.data[52] != 63) {
        FAIL("Expected padding LCID=63 at byte 52, got " + std::to_string(tb.data[52]));
        return;
    }

    auto recovered = mac.process_rx(tb);
    if (recovered.size() != 1 || !buffers_equal(sdu, recovered[0])) {
        FAIL("Recovered SDU mismatch after padding");
        return;
    }

    PASS();
}

int main() {
    std::cout << "==============================\n";
    std::cout << " MAC Layer Unit Tests\n";
    std::cout << "==============================\n";

    test_single_small_sdu();
    test_single_large_sdu();
    test_multiple_sdus();
    test_mixed_sizes();
    test_padding();

    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
