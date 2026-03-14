// ============================================================
// test_integration.cpp — End-to-end integration test
//
// Exercises the full protocol stack pipeline:
//   IP → PDCP TX → RLC TX → MAC TX → Transport Block
//   Transport Block → MAC RX → RLC RX → PDCP RX → IP
//
// Runs multiple scenarios:
//   - Default config (1400-byte packets)
//   - Small packets (100 bytes — no RLC segmentation)
//   - Large packets near TB size
//   - Multiple packets in sequence
//   - Various TB sizes
// ============================================================

#include "common.h"
#include "ip_generator.h"
#include "pdcp.h"
#include "rlc.h"
#include "mac.h"

#include <iostream>
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

/// Run the full uplink→loopback→downlink pipeline for one packet.
/// Returns true if the recovered IP packet matches the original.
static bool run_pipeline(Config& cfg, uint32_t seq,
                          IpGenerator& ip_gen, PdcpLayer& pdcp,
                          RlcLayer& rlc, MacLayer& mac) {

    // Generate original IP packet
    ByteBuffer original_ip = ip_gen.generate_packet(seq);

    // ---- Uplink ----
    ByteBuffer pdcp_pdu = pdcp.process_tx(original_ip);
    std::vector<ByteBuffer> rlc_pdus = rlc.process_tx(pdcp_pdu);
    ByteBuffer transport_block = mac.process_tx(rlc_pdus);

    // ---- Loopback → Downlink ----
    std::vector<ByteBuffer> rx_rlc_pdus = mac.process_rx(transport_block);

    ByteBuffer recovered_pdcp_pdu;
    for (const auto& rpdu : rx_rlc_pdus) {
        auto reassembled = rlc.process_rx(rpdu);
        for (auto& sdu : reassembled) {
            recovered_pdcp_pdu = std::move(sdu);
        }
    }

    ByteBuffer recovered_ip = pdcp.process_rx(recovered_pdcp_pdu);

    return ip_gen.verify_packet(original_ip, recovered_ip);
}

// ---- Test: default configuration (1400-byte packets) ----
static void test_default_config() {
    TEST("Default config (1400-byte packets, 10 packets)");

    Config cfg;  // all defaults
    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

// ---- Test: small packets (no RLC segmentation) ----
static void test_small_packets() {
    TEST("Small packets (100 bytes, no RLC segmentation)");

    Config cfg;
    cfg.ip_packet_size = 100;
    cfg.num_packets = 5;

    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

// ---- Test: with 18-bit PDCP SN ----
static void test_18bit_sn() {
    TEST("18-bit PDCP SN mode");

    Config cfg;
    cfg.pdcp_sn_length = 18;
    cfg.ip_packet_size = 500;
    cfg.num_packets = 5;

    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

// ---- Test: ciphering and integrity disabled ----
static void test_no_security() {
    TEST("No ciphering, no integrity");

    Config cfg;
    cfg.ciphering_enabled = false;
    cfg.integrity_enabled = false;
    cfg.ip_packet_size = 300;
    cfg.num_packets = 5;

    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

// ---- Test: large TB size (4096 bytes) ----
static void test_large_tb() {
    TEST("Large TB size (4096 bytes)");

    Config cfg;
    cfg.transport_block_size = 4096;
    cfg.ip_packet_size = 1400;
    cfg.num_packets = 5;

    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

// ---- Test: small TB size (requires many RLC segments to fit) ----
static void test_small_tb() {
    TEST("Small TB (1024 bytes), small packets (200 bytes)");

    Config cfg;
    cfg.transport_block_size = 1024;
    cfg.ip_packet_size = 200;
    cfg.rlc_max_pdu_size = 150;
    cfg.num_packets = 5;

    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

// ---- Test: stress test with 50 packets ----
static void test_stress() {
    TEST("Stress test (50 packets, 1400 bytes each)");

    Config cfg;
    cfg.num_packets = 50;
    cfg.ip_packet_size = 1400;

    IpGenerator ip_gen(cfg);
    PdcpLayer pdcp(cfg);
    RlcLayer rlc(cfg);
    MacLayer mac(cfg);

    for (uint32_t i = 0; i < cfg.num_packets; i++) {
        if (!run_pipeline(cfg, i, ip_gen, pdcp, rlc, mac)) {
            FAIL("Packet " + std::to_string(i) + " failed");
            return;
        }
    }
    PASS();
}

int main() {
    std::cout << "==============================\n";
    std::cout << " Integration Tests\n";
    std::cout << "==============================\n";

    test_default_config();
    test_small_packets();
    test_18bit_sn();
    test_no_security();
    test_large_tb();
    test_small_tb();
    test_stress();

    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
