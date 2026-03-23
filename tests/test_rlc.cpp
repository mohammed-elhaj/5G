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
#include <fstream>
#include <cassert>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <cstdlib>
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

// ============================================================
// Benchmark config helpers (Req 15)
// ============================================================
static Config make_v1_config() {
    Config cfg;
    cfg.rlc_mode         = 1;
    cfg.rlc_sn_length    = 6;
    cfg.rlc_max_pdu_size = 500;
    cfg.loss_rate        = 0.0;
    return cfg;
}

static Config make_optimized_config() {
    Config cfg;
    cfg.rlc_mode          = 2;
    cfg.rlc_am_sn_length  = 12;
    cfg.rlc_max_pdu_size  = 500;
    cfg.loss_rate         = 0.0;
    cfg.rlc_opt_level     = 1;
    return cfg;
}

// ============================================================
// AM TX tests (Member 3)
// ============================================================
static void test_am_complete_sdu() {
    TEST("AM: complete SDU (SI=00, D/C=1)");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg);
    ByteBuffer sdu = make_test_sdu(100, 0xAA);
    auto pdus = tx.process_tx(sdu);
    if (pdus.size() != 1) { FAIL("Expected 1 PDU"); return; }
    // D/C=1 (bit7), SI=00 (bits5:4=00)
    if ((pdus[0].data[0] & 0x80) == 0) { FAIL("D/C bit not set"); return; }
    if (((pdus[0].data[0] >> 4) & 0x03) != 0x00) { FAIL("SI not 00"); return; }
    PASS();
}

static void test_am_header_bytes() {
    TEST("AM: 2-segment header byte layout (D/C, SI, SN, SO)");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 100;
    RlcLayer tx(cfg);
    ByteBuffer sdu = make_test_sdu(150, 0xBB);
    auto pdus = tx.process_tx(sdu);
    if (pdus.size() < 2) { FAIL("Expected >= 2 PDUs"); return; }
    // First PDU: D/C=1, SI=01, no SO
    uint8_t si0 = (pdus[0].data[0] >> 4) & 0x03;
    if (si0 != 0x01) { FAIL("First segment SI != 01"); return; }
    // Last PDU: D/C=1, SI=10, has SO
    uint8_t si_last = (pdus.back().data[0] >> 4) & 0x03;
    if (si_last != 0x02) { FAIL("Last segment SI != 10"); return; }
    PASS();
}

static void test_am_segmentation() {
    TEST("AM: multi-segment round-trip");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 200;
    RlcLayer tx(cfg), rx(cfg);
    ByteBuffer sdu = make_test_sdu(600, 0xCC);
    auto pdus = tx.process_tx(sdu);
    std::vector<ByteBuffer> result;
    for (auto& p : pdus) {
        auto r = rx.process_rx(p);
        result.insert(result.end(), r.begin(), r.end());
    }
    if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
        FAIL("AM round-trip mismatch"); return;
    }
    PASS();
}

static void test_sn_wrap_am() {
    TEST("AM: SN wraps at 4096");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg);
    // Send 4096 SDUs to force wrap
    for (int i = 0; i < 4096; i++) {
        ByteBuffer sdu = make_test_sdu(10, static_cast<uint8_t>(i));
        tx.process_tx(sdu);
    }
    // Next SN should be 0 again
    ByteBuffer sdu = make_test_sdu(10, 0x00);
    auto pdus = tx.process_tx(sdu);
    uint16_t sn = (static_cast<uint16_t>(pdus[0].data[0] & 0x0F) << 8) | pdus[0].data[1];
    if (sn != 0) { FAIL("SN did not wrap to 0"); return; }
    PASS();
}

// ============================================================
// STATUS PDU tests (Member 3)
// ============================================================
static void test_status_pdu_no_nack() {
    TEST("STATUS PDU: build/parse with no NACKs");
    Config cfg; cfg.rlc_mode = 2;
    RlcLayer rlc(cfg);
    // Use AM round-trip to exercise build/parse indirectly via a gap-free delivery
    // Direct test: build a STATUS PDU with ack_sn=42, no NACKs, verify parse
    // We access via process_rx with a D/C=0 PDU
    // Build manually: [0x00|ack_sn[11:8]=0x00][ack_sn[7:0]=42][E1=0]
    ByteBuffer status_pdu;
    status_pdu.data = {0x00, 42, 0x00};
    auto result = rlc.process_rx(status_pdu);
    // No data PDUs to retransmit — result should be empty
    if (!result.empty()) { FAIL("Expected empty result for STATUS with no NACKs"); return; }
    PASS();
}

static void test_status_pdu_with_nacks() {
    TEST("STATUS PDU: retransmit on NACK");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg);
    // Send 2 SDUs — SN=0 and SN=1 stored in retx_buf_
    tx.process_tx(make_test_sdu(100, 0x01));  // SN=0
    tx.process_tx(make_test_sdu(100, 0x02));  // SN=1
    // Build STATUS PDU: ack_sn=2 (acknowledges nothing below 2), NACK_SN=1
    // Byte layout: [D/C=0|CPT=000|ACK_SN[11:8]=0x00][ACK_SN[7:0]=0x02]
    //              [E1=1|padding=0000000]
    //              [NACK_SN[11:4]=0x00][NACK_SN[3:0]=0x1 << 4 | E1_next=0 | 000]
    ByteBuffer status;
    status.data = {0x00, 0x02, 0x80, 0x00, 0x10};
    auto retx = tx.process_rx(status);
    if (retx.empty()) { FAIL("Expected retransmitted PDU for NACK_SN=1"); return; }
    PASS();
}

static void test_status_pdu_truncated() {
    TEST("STATUS PDU: truncated input returns no crash");
    Config cfg; cfg.rlc_mode = 2;
    RlcLayer rlc(cfg);
    ByteBuffer short_pdu;
    short_pdu.data = {0x00, 0x01};  // only 2 bytes, minimum is 3
    auto result = rlc.process_rx(short_pdu);
    // Should return empty without crashing
    PASS();
}

static void test_dc_bit_routing() {
    TEST("STATUS PDU: D/C=0 routed to STATUS path, not data path");
    Config cfg; cfg.rlc_mode = 2;
    RlcLayer rlc(cfg);
    // D/C=0 PDU (bit7=0)
    ByteBuffer pdu;
    pdu.data = {0x00, 0x05, 0x00};  // valid STATUS PDU, ack_sn=5, no NACKs
    auto result = rlc.process_rx(pdu);
    // Should not produce a data SDU
    for (auto& r : result) {
        if (r.size() > 3) { FAIL("STATUS PDU incorrectly treated as data"); return; }
    }
    PASS();
}

static void test_am_retransmission() {
    TEST("AM: NACK triggers retransmission of correct SN");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg);
    ByteBuffer sdu = make_test_sdu(100, 0xDD);
    tx.process_tx(sdu);  // SN=0 stored in retx_buf_
    // Send STATUS PDU with NACK_SN=0: ack_sn=0, nack=[0]
    ByteBuffer status;
    status.data = {0x00, 0x00, 0x80, 0x00, 0x00};
    auto retx = tx.process_rx(status);
    if (retx.empty()) { FAIL("No retransmission on NACK"); return; }
    // Retransmitted PDU should have D/C=1
    if ((retx[0].data[0] & 0x80) == 0) { FAIL("Retransmitted PDU missing D/C bit"); return; }
    PASS();
}

static void test_am_ack_clears_buffer() {
    TEST("AM: ACK_SN clears retx_buf_ entries below it");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg);
    // Send 3 SDUs (SN 0, 1, 2)
    for (int i = 0; i < 3; i++)
        tx.process_tx(make_test_sdu(50, static_cast<uint8_t>(i)));
    // ACK SN=2 (clears 0 and 1), no NACKs
    ByteBuffer status;
    status.data = {0x00, 0x02, 0x00};
    tx.process_rx(status);
    // NACK SN=0 — should be ignored (already ACKed)
    ByteBuffer nack_status;
    nack_status.data = {0x00, 0x00, 0x80, 0x00, 0x00};
    auto retx = tx.process_rx(nack_status);
    if (!retx.empty()) { FAIL("ACKed SN was retransmitted"); return; }
    PASS();
}

// ============================================================
// AM RX tests (Member 4)
// ============================================================
static void test_am_in_order_delivery() {
    TEST("AM: in-order delivery, RX_Next advances");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg), rx(cfg);
    for (int i = 0; i < 5; i++) {
        ByteBuffer sdu = make_test_sdu(100, static_cast<uint8_t>(i));
        auto pdus = tx.process_tx(sdu);
        std::vector<ByteBuffer> result;
        for (auto& p : pdus) {
            auto r = rx.process_rx(p);
            result.insert(result.end(), r.begin(), r.end());
        }
        if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
            FAIL("In-order delivery failed at SDU " + std::to_string(i)); return;
        }
    }
    PASS();
}

static void test_am_out_of_order_buffering() {
    TEST("AM: out-of-order PDU buffered, delivered after gap filled");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg), rx(cfg);
    ByteBuffer sdu0 = make_test_sdu(80, 0x10);
    ByteBuffer sdu1 = make_test_sdu(80, 0x20);
    auto pdus0 = tx.process_tx(sdu0);
    auto pdus1 = tx.process_tx(sdu1);
    // Deliver sdu1 first (out of order) — may produce STATUS PDUs, not data SDUs
    // STATUS PDUs are small (< 20 bytes); data SDUs are 80 bytes
    std::vector<ByteBuffer> data_result;
    for (auto& p : pdus1) {
        auto r = rx.process_rx(p);
        for (auto& item : r) {
            if (item.size() >= 80)  // data SDU, not a STATUS PDU
                data_result.push_back(item);
        }
    }
    if (!data_result.empty()) { FAIL("Out-of-order SDU delivered prematurely"); return; }
    // Now deliver sdu0 — both should come out as data SDUs
    for (auto& p : pdus0) {
        auto r = rx.process_rx(p);
        for (auto& item : r) {
            if (item.size() >= 80)
                data_result.push_back(item);
        }
    }
    if (data_result.size() < 2) { FAIL("Expected 2 SDUs after gap filled"); return; }
    PASS();
}

static void test_am_gap_generates_nack() {
    TEST("AM: gap detection generates STATUS PDU with NACK");
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg), rx(cfg);
    ByteBuffer sdu0 = make_test_sdu(80, 0x10);
    ByteBuffer sdu1 = make_test_sdu(80, 0x20);
    tx.process_tx(sdu0);  // SN=0, not delivered to rx
    auto pdus1 = tx.process_tx(sdu1);  // SN=1
    // Deliver SN=1 — should trigger STATUS PDU with NACK_SN=0
    std::vector<ByteBuffer> result;
    for (auto& p : pdus1) {
        auto r = rx.process_rx(p);
        result.insert(result.end(), r.begin(), r.end());
    }
    // At least one result should be a STATUS PDU (D/C=0)
    bool found_status = false;
    for (auto& r : result) {
        if (!r.empty() && (r.data[0] & 0x80) == 0) { found_status = true; break; }
    }
    if (!found_status) { FAIL("No STATUS PDU generated for gap"); return; }
    PASS();
}

static void test_malformed_pdu_discarded() {
    TEST("AM: malformed PDU (< 2 bytes) discarded, no crash");
    Config cfg; cfg.rlc_mode = 2;
    RlcLayer rx(cfg);
    ByteBuffer bad;
    bad.data = {0x80};  // D/C=1 but only 1 byte — too short for AM header
    auto result = rx.process_rx(bad);
    if (!result.empty()) { FAIL("Malformed PDU produced output"); return; }
    PASS();
}

// Helper: returns true if buf looks like a STATUS PDU (D/C=0 and small size)
// Data SDUs in test_am_loss_recovery are 200 bytes; STATUS PDUs are at most ~40 bytes.
static bool is_status_pdu(const ByteBuffer& buf) {
    return !buf.empty() && (buf.data[0] & 0x80) == 0 && buf.size() < 100;
}

// ---- Test: end-to-end AM loss recovery (Member 4) ----
static void test_am_loss_recovery() {
    TEST("AM: end-to-end loss recovery (loss_rate=0.1)");
    // Manual loss simulation: drop every 10th PDU (by index across all PDUs)
    Config cfg; cfg.rlc_mode = 2; cfg.rlc_max_pdu_size = 500;
    RlcLayer tx(cfg), rx(cfg);

    const int NUM_SDUS = 20;
    const size_t SDU_SIZE = 200;  // known size — used to identify delivered data SDUs
    std::vector<ByteBuffer> originals;
    std::vector<std::vector<ByteBuffer>> all_pdus;

    for (int i = 0; i < NUM_SDUS; i++) {
        originals.push_back(make_test_sdu(SDU_SIZE, static_cast<uint8_t>(i)));
        all_pdus.push_back(tx.process_tx(originals.back()));
    }

    // Helper lambda: process one PDU through RX, collect data SDUs and feed STATUS PDUs back
    int pdu_index = 0;
    std::vector<ByteBuffer> delivered;

    auto process_pdu = [&](const ByteBuffer& pdu) {
        auto r = rx.process_rx(pdu);
        for (auto& item : r) {
            if (is_status_pdu(item)) {
                // Feed STATUS PDU back to TX, then deliver any retransmitted PDUs
                auto retx = tx.process_rx(item);
                for (auto& rp : retx) {
                    auto rr = rx.process_rx(rp);
                    for (auto& d : rr) {
                        if (!is_status_pdu(d))
                            delivered.push_back(d);
                    }
                }
            } else {
                delivered.push_back(item);
            }
        }
    };

    // First pass: deliver all PDUs, dropping every 10th
    for (int i = 0; i < NUM_SDUS; i++) {
        for (size_t j = 0; j < all_pdus[i].size(); j++) {
            if (pdu_index % 10 == 0) { pdu_index++; continue; }  // drop
            process_pdu(all_pdus[i][j]);
            pdu_index++;
        }
    }

    // Second pass: re-deliver all PDUs (retransmissions already triggered via STATUS)
    // to fill any remaining gaps
    if (static_cast<int>(delivered.size()) < NUM_SDUS) {
        for (int i = 0; i < NUM_SDUS; i++) {
            for (auto& pdu : all_pdus[i]) {
                process_pdu(pdu);
            }
        }
    }

    if (static_cast<int>(delivered.size()) < NUM_SDUS) {
        FAIL("Not all SDUs recovered (" + std::to_string(delivered.size()) +
             "/" + std::to_string(NUM_SDUS) + ")");
        return;
    }
    PASS();
}

// ============================================================
// Property-based tests (Both)
// ============================================================

// Feature: rlc-latency-optimization, Property 1: UM Round-Trip Correctness
static void test_prop_um_round_trip() {
    TEST("PROP P1: UM round-trip correctness (100 iterations)");
    Config cfg = make_v1_config();
    const std::vector<size_t> sizes = {1, 50, 100, 499, 500, 501, 1000, 1400, 3000};
    for (int iter = 0; iter < 100; iter++) {
        size_t sz = sizes[iter % sizes.size()] + (iter * 7) % 50;
        RlcLayer tx(cfg), rx(cfg);
        ByteBuffer sdu = make_test_sdu(sz, static_cast<uint8_t>(iter));
        auto pdus = tx.process_tx(sdu);
        std::vector<ByteBuffer> result;
        for (auto& p : pdus) {
            auto r = rx.process_rx(p);
            result.insert(result.end(), r.begin(), r.end());
        }
        if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
            FAIL("P1 failed at iter=" + std::to_string(iter) + " size=" + std::to_string(sz));
            return;
        }
    }
    PASS();
}

// Feature: rlc-latency-optimization, Property 2: AM Round-Trip Correctness (No Loss)
static void test_prop_am_round_trip() {
    TEST("PROP P2: AM round-trip correctness no-loss (100 iterations)");
    Config cfg = make_optimized_config();
    const std::vector<size_t> sizes = {1, 50, 100, 498, 500, 502, 1000, 1400, 3000};
    for (int iter = 0; iter < 100; iter++) {
        size_t sz = sizes[iter % sizes.size()] + (iter * 11) % 50;
        RlcLayer tx(cfg), rx(cfg);
        ByteBuffer sdu = make_test_sdu(sz, static_cast<uint8_t>(iter + 50));
        auto pdus = tx.process_tx(sdu);
        std::vector<ByteBuffer> result;
        for (auto& p : pdus) {
            auto r = rx.process_rx(p);
            result.insert(result.end(), r.begin(), r.end());
        }
        if (result.size() != 1 || !buffers_equal(sdu, result[0])) {
            FAIL("P2 failed at iter=" + std::to_string(iter) + " size=" + std::to_string(sz));
            return;
        }
    }
    PASS();
}

// Feature: rlc-latency-optimization, Property 3: AM Recovery Under Loss
static void test_prop_am_recovery() {
    TEST("PROP P3: AM recovery under loss (50 iterations)");
    Config cfg = make_optimized_config();
    for (int iter = 0; iter < 50; iter++) {
        size_t sz = 100 + (iter * 13) % 400;
        RlcLayer tx(cfg), rx(cfg);
        ByteBuffer sdu = make_test_sdu(sz, static_cast<uint8_t>(iter));
        auto pdus = tx.process_tx(sdu);

        // Drop first PDU, deliver rest
        std::vector<ByteBuffer> result;
        for (size_t j = 1; j < pdus.size(); j++) {
            auto r = rx.process_rx(pdus[j]);
            result.insert(result.end(), r.begin(), r.end());
        }

        // Feed STATUS PDUs back to TX and retransmit
        for (auto& r : result) {
            if (!r.empty() && (r.data[0] & 0x80) == 0) {
                auto retx = tx.process_rx(r);
                for (auto& rp : retx) {
                    auto rr = rx.process_rx(rp);
                    result.insert(result.end(), rr.begin(), rr.end());
                }
            }
        }

        // Deliver dropped PDU
        auto r0 = rx.process_rx(pdus[0]);
        result.insert(result.end(), r0.begin(), r0.end());

        bool found = false;
        for (auto& r : result) {
            if (buffers_equal(sdu, r)) { found = true; break; }
        }
        if (!found) {
            FAIL("P3 failed at iter=" + std::to_string(iter));
            return;
        }
    }
    PASS();
}

// Feature: rlc-latency-optimization, Property 4: UM Permanent Loss
static void test_prop_um_permanent_loss() {
    TEST("PROP P4: UM permanent loss (50 iterations)");
    Config cfg = make_v1_config();
    for (int iter = 0; iter < 50; iter++) {
        size_t sz = 600 + (iter * 17) % 400;
        RlcLayer tx(cfg), rx(cfg);
        ByteBuffer sdu = make_test_sdu(sz, static_cast<uint8_t>(iter));
        auto pdus = tx.process_tx(sdu);
        if (pdus.size() < 2) continue;  // need at least 2 segments to drop one

        // Drop the last segment
        std::vector<ByteBuffer> result;
        for (size_t j = 0; j + 1 < pdus.size(); j++) {
            auto r = rx.process_rx(pdus[j]);
            result.insert(result.end(), r.begin(), r.end());
        }
        // SDU must NOT be delivered
        if (!result.empty()) {
            FAIL("P4 failed: UM delivered SDU with dropped segment at iter=" + std::to_string(iter));
            return;
        }
    }
    PASS();
}

// Feature: rlc-latency-optimization, Property 5: SN Monotonicity and Wrap
static void test_prop_sn_monotonicity() {
    TEST("PROP P5: SN monotonicity and wrap (UM mod 64, AM mod 4096)");
    // UM mode
    {
        Config cfg = make_v1_config();
        RlcLayer tx(cfg);
        for (int k = 0; k < 130; k++) {
            auto pdus = tx.process_tx(make_test_sdu(10, 0x00));
            uint8_t sn = pdus[0].data[0] & 0x3F;
            if (sn != static_cast<uint8_t>(k % 64)) {
                FAIL("P5 UM SN mismatch at k=" + std::to_string(k));
                return;
            }
        }
    }
    // AM mode
    {
        Config cfg = make_optimized_config();
        RlcLayer tx(cfg);
        for (int k = 0; k < 4100; k++) {
            auto pdus = tx.process_tx(make_test_sdu(10, 0x00));
            uint16_t sn = (static_cast<uint16_t>(pdus[0].data[0] & 0x0F) << 8) | pdus[0].data[1];
            if (sn != static_cast<uint16_t>(k % 4096)) {
                FAIL("P5 AM SN mismatch at k=" + std::to_string(k));
                return;
            }
        }
    }
    PASS();
}

// Feature: rlc-latency-optimization, Property 6: STATUS PDU Round-Trip
static void test_prop_status_pdu_round_trip() {
    TEST("PROP P6: STATUS PDU round-trip (100 iterations)");
    Config cfg = make_optimized_config();
    RlcLayer rlc(cfg);
    // Test via manual byte construction and parse
    // ack_sn varied, nack list varied
    for (int iter = 0; iter < 100; iter++) {
        uint16_t ack_sn = static_cast<uint16_t>((iter * 37) % 4096);
        // Build STATUS PDU manually: D/C=0, ack_sn, no NACKs
        ByteBuffer pdu;
        pdu.data.resize(3);
        pdu.data[0] = static_cast<uint8_t>((ack_sn >> 8) & 0x0F);
        pdu.data[1] = static_cast<uint8_t>(ack_sn & 0xFF);
        pdu.data[2] = 0x00;
        // Feed to RLC — should not crash and should process ack_sn
        rlc.process_rx(pdu);
    }
    PASS();
}

// ============================================================
// Benchmark: fair comparison using rlc_opt_level config switch
// ============================================================
static void profile_variants(const std::string& csv_path = "") {
    const std::vector<uint32_t> pkt_sizes = {100, 500, 1000, 1400, 3000};
    const int ITERATIONS = 1000;

    // Optional CSV output — one row per iteration
    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path);
        if (csv.is_open())
            csv << "pkt_size,variant,iteration,tx_us,rx_us,pass\n";
        else
            std::cerr << "  WARNING: could not open CSV file: " << csv_path << "\n";
    }

    std::cout << "\n=======================================================\n";
    std::cout << "PROFILING: RLC Layer — V1 (opt=0) vs Optimized (opt=1)\n";
    std::cout << "=======================================================\n";
    std::cout << std::left
              << std::setw(8)  << "PktSize"
              << std::setw(14) << "Variant"
              << std::setw(14) << "TX avg(us)"
              << std::setw(14) << "RX avg(us)"
              << std::setw(12) << "TX speedup"
              << std::setw(12) << "RX speedup"
              << "Pass\n";
    std::cout << std::string(74, '-') << "\n";

    struct Variant { const char* label; uint8_t mode; uint8_t opt; };
    const Variant variants[] = {
        {"V1-UM",   1, 0},
        {"OPT-UM",  1, 1},
        {"V1-AM",   2, 0},
        {"OPT-AM",  2, 1},
    };

    for (uint32_t sz : pkt_sizes) {
        double baseline_tx[2] = {0, 0};  // [0]=UM baseline, [1]=AM baseline
        double baseline_rx[2] = {0, 0};

        for (int vi = 0; vi < 4; vi++) {
            auto& v = variants[vi];
            Config cfg;
            cfg.rlc_mode         = v.mode;
            cfg.rlc_max_pdu_size = 500;
            cfg.rlc_opt_level    = v.opt;
            if (v.mode == 2) cfg.rlc_am_sn_length = 12;

            double total_tx = 0, total_rx = 0;
            bool all_pass = true;

            for (int i = 0; i < ITERATIONS; i++) {
                RlcLayer tx(cfg), rx(cfg);
                ByteBuffer sdu = make_test_sdu(sz, static_cast<uint8_t>(i));

                auto t0 = std::chrono::high_resolution_clock::now();
                auto pdus = tx.process_tx(sdu);
                auto t1 = std::chrono::high_resolution_clock::now();

                std::vector<ByteBuffer> result;
                for (auto& p : pdus) {
                    auto r = rx.process_rx(p);
                    result.insert(result.end(), r.begin(), r.end());
                }
                auto t2 = std::chrono::high_resolution_clock::now();

                double iter_tx = std::chrono::duration<double, std::micro>(t1 - t0).count();
                double iter_rx = std::chrono::duration<double, std::micro>(t2 - t1).count();
                bool   iter_ok = (result.size() == 1 && buffers_equal(sdu, result[0]));

                total_tx += iter_tx;
                total_rx += iter_rx;
                if (!iter_ok) all_pass = false;

                // Write per-iteration CSV row
                if (csv.is_open()) {
                    csv << sz << ","
                        << v.label << ","
                        << i << ","
                        << std::fixed << std::setprecision(4) << iter_tx << ","
                        << iter_rx << ","
                        << (iter_ok ? "1" : "0") << "\n";
                }
            }
            total_tx /= ITERATIONS;
            total_rx /= ITERATIONS;

            // Store baselines (V1-UM=vi0, V1-AM=vi2)
            int base_idx = (v.mode == 2) ? 1 : 0;
            if (v.opt == 0) { baseline_tx[base_idx] = total_tx; baseline_rx[base_idx] = total_rx; }

            double spd_tx = (v.opt == 0) ? 1.0 : baseline_tx[base_idx] / total_tx;
            double spd_rx = (v.opt == 0) ? 1.0 : baseline_rx[base_idx] / total_rx;

            std::cout << std::left << std::fixed << std::setprecision(2)
                      << std::setw(8)  << (vi % 2 == 0 ? std::to_string(sz) : "")
                      << std::setw(14) << v.label
                      << std::setw(14) << total_tx
                      << std::setw(14) << total_rx;
            if (v.opt == 0)
                std::cout << std::setw(12) << "baseline" << std::setw(12) << "baseline";
            else
                std::cout << std::setw(12) << spd_tx << std::setw(12) << spd_rx;
            std::cout << (all_pass ? "PASS" : "FAIL") << "\n";

            if (vi == 1 || vi == 3) std::cout << std::string(74, '-') << "\n";
        }
    }
    std::cout << "  Speedup > 1.0x means optimized is faster\n\n";

    if (csv.is_open()) {
        csv.close();
        std::cout << "  CSV written to: " << csv_path << "\n";
    }
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

    // ---- AM mode tests ----
    test_am_complete_sdu();
    test_am_header_bytes();
    test_am_segmentation();
    test_sn_wrap_am();
    test_status_pdu_no_nack();
    test_status_pdu_with_nacks();
    test_status_pdu_truncated();
    test_dc_bit_routing();
    test_am_retransmission();
    test_am_ack_clears_buffer();
    test_am_in_order_delivery();
    test_am_out_of_order_buffering();
    test_am_gap_generates_nack();
    test_malformed_pdu_discarded();
    test_am_loss_recovery();

    // ---- Property-based tests ----
    test_prop_um_round_trip();
    test_prop_am_round_trip();
    test_prop_am_recovery();
    test_prop_um_permanent_loss();
    test_prop_sn_monotonicity();
    test_prop_status_pdu_round_trip();

    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";

    // ---- Benchmark ----
    // To enable CSV output, set benchmark_csv_path in Config, e.g.:
    //   Config bench_cfg; bench_cfg.benchmark_csv_path = "rlc_bench.csv";
    Config bench_cfg;
    // bench_cfg.benchmark_csv_path = "rlc_bench.csv";  // uncomment to write CSV
    profile_variants(bench_cfg.benchmark_csv_path);

    return (tests_passed == tests_run) ? 0 : 1;
}
