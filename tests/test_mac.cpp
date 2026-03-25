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

static void test_single_small_sdu() {
    TEST("Single small SDU (100 bytes, 8-bit L)");

    Config cfg;
    cfg.transport_block_size = 1024;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);
    ByteBuffer sdu = make_test_sdu(100, 0xAA);

    ByteBuffer tb = mac.process_tx({sdu});

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

static void test_mixed_sizes() {
    TEST("Mixed SDU sizes (50, 300, 150 bytes)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);

    std::vector<ByteBuffer> sdus;
    sdus.push_back(make_test_sdu(50,  0xAA));
    sdus.push_back(make_test_sdu(300, 0xBB));
    sdus.push_back(make_test_sdu(150, 0xCC));

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

static void test_padding() {
    TEST("Padding fills remaining TB space");

    Config cfg;
    cfg.transport_block_size = 512;
    cfg.logical_channel_id   = 4;

    MacLayer mac(cfg);
    ByteBuffer sdu = make_test_sdu(50, 0xDD);

    ByteBuffer tb = mac.process_tx({sdu});

    if (tb.size() != 512) {
        FAIL("TB size is " + std::to_string(tb.size()));
        return;
    }

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

static void test_multi_lcid_mux_demux() {
    TEST("Multi-LCID mux/demux (LCID 4 + LCID 5, 2 SDUs each)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.lcp_enabled          = false;
    MacLayer mac(cfg);

    ByteBuffer sdu4a = make_test_sdu(80,  0x11);
    ByteBuffer sdu4b = make_test_sdu(90,  0x22);
    ByteBuffer sdu5a = make_test_sdu(70,  0x33);
    ByteBuffer sdu5b = make_test_sdu(100, 0x44);

    LcData ch4; ch4.lcid = 4; ch4.priority = 1; ch4.pbr_bytes = 0xFFFFFFFF;
    ch4.sdus = {sdu4a, sdu4b};
    LcData ch5; ch5.lcid = 5; ch5.priority = 2; ch5.pbr_bytes = 0xFFFFFFFF;
    ch5.sdus = {sdu5a, sdu5b};

    ByteBuffer tb = mac.process_tx({ch4, ch5});
    auto tagged = mac.process_rx_multi(tb);

    if (tagged.size() != 4) {
        FAIL("Expected 4 tagged SDUs, got " + std::to_string(tagged.size()));
        return;
    }
    if (tagged[0].first != 4 || !buffers_equal(tagged[0].second, sdu4a)) { FAIL("SDU0 mismatch"); return; }
    if (tagged[1].first != 4 || !buffers_equal(tagged[1].second, sdu4b)) { FAIL("SDU1 mismatch"); return; }
    if (tagged[2].first != 5 || !buffers_equal(tagged[2].second, sdu5a)) { FAIL("SDU2 mismatch"); return; }
    if (tagged[3].first != 5 || !buffers_equal(tagged[3].second, sdu5b)) { FAIL("SDU3 mismatch"); return; }

    PASS();
}

static void test_lcp_priority_ordering() {
    TEST("LCP priority ordering (LCID 4 prio=1 appears before LCID 5 prio=2)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.lcp_enabled          = true;
    MacLayer mac(cfg);

    ByteBuffer sdu4 = make_test_sdu(100, 0xAA);
    ByteBuffer sdu5 = make_test_sdu(100, 0xBB);

    LcData ch5; ch5.lcid = 5; ch5.priority = 2; ch5.pbr_bytes = 512; ch5.sdus = {sdu5};
    LcData ch4; ch4.lcid = 4; ch4.priority = 1; ch4.pbr_bytes = 512; ch4.sdus = {sdu4};

    ByteBuffer tb = mac.process_tx({ch5, ch4});

    if ((tb.data[0] & 0x3F) != 4) {
        FAIL("Expected LCID=4 first in TB, got LCID=" + std::to_string(tb.data[0] & 0x3F));
        return;
    }

    auto tagged = mac.process_rx_multi(tb);
    if (tagged.size() != 2 || tagged[0].first != 4 || tagged[1].first != 5) {
        FAIL("Round-trip LCID order incorrect");
        return;
    }

    PASS();
}

static void test_lcp_pbr_quota() {
    TEST("LCP PBR quota (LCID 4 capped at 100B, remainder via round-robin)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.lcp_enabled          = true;
    MacLayer mac(cfg);

    LcData ch4; ch4.lcid = 4; ch4.priority = 1; ch4.pbr_bytes = 100;
    for (int i = 0; i < 5; i++) ch4.sdus.push_back(make_test_sdu(30, static_cast<uint8_t>(i)));
    LcData ch5; ch5.lcid = 5; ch5.priority = 2; ch5.pbr_bytes = 9999;
    ch5.sdus.push_back(make_test_sdu(200, 0xFF));

    ByteBuffer tb = mac.process_tx({ch4, ch5});
    auto tagged = mac.process_rx_multi(tb);

    if (tagged.size() != 6) {
        FAIL("Expected 6 SDUs, got " + std::to_string(tagged.size()));
        return;
    }

    bool ch4_first3 = (tagged[0].first == 4 && tagged[1].first == 4 && tagged[2].first == 4);
    bool ch5_next   = (tagged[3].first == 5);
    bool ch4_last2  = (tagged[4].first == 4 && tagged[5].first == 4);

    if (!ch4_first3 || !ch5_next || !ch4_last2) {
        FAIL("LCP scheduling order incorrect");
        return;
    }

    PASS();
}

static void test_lcp_3channel_roundrobin() {
    TEST("LCP round-robin cycles across 3 channels (LCID 4/5/6, pbr=60B, 4 SDUs each)");

    Config cfg;
    cfg.transport_block_size = 2048;
    cfg.lcp_enabled          = true;
    MacLayer mac(cfg);

    LcData ch4; ch4.lcid = 4; ch4.priority = 1; ch4.pbr_bytes = 60;
    LcData ch5; ch5.lcid = 5; ch5.priority = 2; ch5.pbr_bytes = 60;
    LcData ch6; ch6.lcid = 6; ch6.priority = 3; ch6.pbr_bytes = 60;

    for (int i = 0; i < 4; i++) {
        ch4.sdus.push_back(make_test_sdu(30, static_cast<uint8_t>(0x10 + i)));
        ch5.sdus.push_back(make_test_sdu(30, static_cast<uint8_t>(0x20 + i)));
        ch6.sdus.push_back(make_test_sdu(30, static_cast<uint8_t>(0x30 + i)));
    }

    ByteBuffer tb  = mac.process_tx({ch4, ch5, ch6});
    auto       out = mac.process_rx_multi(tb);

    if (out.size() != 12) {
        FAIL("Expected 12 SDUs, got " + std::to_string(out.size()));
        return;
    }

    const uint8_t pbr_expected[6] = {4, 4, 5, 5, 6, 6};
    for (int i = 0; i < 6; i++) {
        if (out[i].first != pbr_expected[i]) {
            FAIL("PBR phase: pos " + std::to_string(i) +
                 " expected LCID=" + std::to_string(pbr_expected[i]) +
                 " got LCID=" + std::to_string(out[i].first));
            return;
        }
    }

    const uint8_t rr_expected[6] = {4, 5, 6, 4, 5, 6};
    for (int i = 0; i < 6; i++) {
        if (out[6 + i].first != rr_expected[i]) {
            FAIL("RR phase: pos " + std::to_string(6 + i) +
                 " expected LCID=" + std::to_string(rr_expected[i]) +
                 " got LCID=" + std::to_string(out[6 + i].first));
            return;
        }
    }

    const ByteBuffer* expected[12] = {
        &ch4.sdus[0], &ch4.sdus[1],
        &ch5.sdus[0], &ch5.sdus[1],
        &ch6.sdus[0], &ch6.sdus[1],
        &ch4.sdus[2], &ch5.sdus[2], &ch6.sdus[2],
        &ch4.sdus[3], &ch5.sdus[3], &ch6.sdus[3],
    };
    for (int i = 0; i < 12; i++) {
        if (!buffers_equal(out[i].second, *expected[i])) {
            FAIL("Payload mismatch at position " + std::to_string(i));
            return;
        }
    }

    PASS();
}

#include <chrono>
#include <iomanip>

static void profile_variants() {
    const int ITERATIONS = 1000;
    const std::vector<uint32_t> packet_sizes = {100, 500, 1000, 1400, 3000};

    std::cout << "\n========================================" << std::endl;
    std::cout << "PROFILING: MAC Layer" << std::endl;
    std::cout << "========================================\n" << std::endl;

    std::cout << std::left
              << std::setw(12) << "PktSize"
              << std::setw(16) << "Variant"
              << std::setw(14) << "TX avg(us)"
              << std::setw(14) << "RX avg(us)"
              << std::setw(14) << "Overhead(B)"
              << std::setw(10) << "Pass"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (uint32_t pkt_size : packet_sizes) {
        Config cfg;
        cfg.transport_block_size = (pkt_size + 3 > 2048) ? pkt_size + 64 : 2048;
        cfg.logical_channel_id   = 4;
        MacLayer mac(cfg);

        ByteBuffer input = make_test_sdu(pkt_size, 0xAB);

        double total_tx = 0.0, total_rx = 0.0;
        bool all_pass = true;

        for (int i = 0; i < ITERATIONS; i++) {
            auto t0 = std::chrono::high_resolution_clock::now();
            ByteBuffer tb = mac.process_tx({input});
            auto t1 = std::chrono::high_resolution_clock::now();
            auto sdus = mac.process_rx(tb);
            auto t2 = std::chrono::high_resolution_clock::now();

            total_tx += std::chrono::duration<double, std::micro>(t1 - t0).count();
            total_rx += std::chrono::duration<double, std::micro>(t2 - t1).count();

            if (i == 0 && (sdus.empty() || !buffers_equal(sdus[0], input)))
                all_pass = false;
        }

        size_t overhead = (pkt_size > 255) ? 3 : 2;

        double efficiency = static_cast<double>(pkt_size) /
                            static_cast<double>(cfg.transport_block_size) * 100.0;

        std::cout << std::left
                  << std::setw(12) << pkt_size
                  << std::setw(16) << "V1-Baseline"
                  << std::setw(14) << std::fixed << std::setprecision(2)
                  << (total_tx / ITERATIONS)
                  << std::setw(14) << (total_rx / ITERATIONS)
                  << std::setw(14) << overhead
                  << std::setw(10) << (all_pass ? "PASS" : "FAIL")
                  << std::endl;

        if (pkt_size == 1400) {
            std::cout << "  MAC Efficiency (pkt=" << pkt_size << "B, TB=2048B): "
                      << std::fixed << std::setprecision(1) << efficiency << "%\n";
        }

        {
            Config cfg2;
            cfg2.transport_block_size = (pkt_size + 3 > 2048) ? pkt_size * 2 + 64 : 2048;
            cfg2.num_logical_channels = 2;
            cfg2.lcp_enabled          = false;
            MacLayer mac2(cfg2);

            uint32_t half = pkt_size / 2;
            ByteBuffer sdu4 = make_test_sdu(half,           0xAB);
            ByteBuffer sdu5 = make_test_sdu(pkt_size - half, 0xCD);

            LcData ch4; ch4.lcid = 4; ch4.priority = 1; ch4.pbr_bytes = 0xFFFFFFFF; ch4.sdus = {sdu4};
            LcData ch5; ch5.lcid = 5; ch5.priority = 2; ch5.pbr_bytes = 0xFFFFFFFF; ch5.sdus = {sdu5};

            double total_tx2 = 0.0, total_rx2 = 0.0;
            bool all_pass2 = true;

            for (int i = 0; i < ITERATIONS; i++) {
                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer tb = mac2.process_tx({ch4, ch5});
                auto t1 = std::chrono::high_resolution_clock::now();
                auto tagged = mac2.process_rx_multi(tb);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx2 += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx2 += std::chrono::duration<double, std::micro>(t2 - t1).count();

                if (i == 0) {
                    if (tagged.size() != 2) { all_pass2 = false; continue; }
                    if (!buffers_equal(tagged[0].second, sdu4)) all_pass2 = false;
                    if (!buffers_equal(tagged[1].second, sdu5)) all_pass2 = false;
                }
            }

            size_t overhead2 = (half > 255 ? 3 : 2) + ((pkt_size - half) > 255 ? 3 : 2);
            std::cout << std::left
                      << std::setw(12) << pkt_size
                      << std::setw(16) << "Multi-LCID"
                      << std::setw(14) << std::fixed << std::setprecision(2)
                      << (total_tx2 / ITERATIONS)
                      << std::setw(14) << (total_rx2 / ITERATIONS)
                      << std::setw(14) << overhead2
                      << std::setw(10) << (all_pass2 ? "PASS" : "FAIL")
                      << std::endl;
        }

        {
            Config cfg3;
            cfg3.transport_block_size = (pkt_size + 3 > 2048) ? pkt_size * 2 + 64 : 2048;
            cfg3.num_logical_channels = 2;
            cfg3.lcp_enabled          = true;
            MacLayer mac3(cfg3);

            uint32_t half = pkt_size / 2;
            ByteBuffer sdu4 = make_test_sdu(half,           0xAB);
            ByteBuffer sdu5 = make_test_sdu(pkt_size - half, 0xCD);

            LcData ch4; ch4.lcid = 4; ch4.priority = 1; ch4.pbr_bytes = pkt_size; ch4.sdus = {sdu4};
            LcData ch5; ch5.lcid = 5; ch5.priority = 2; ch5.pbr_bytes = pkt_size; ch5.sdus = {sdu5};

            double total_tx3 = 0.0, total_rx3 = 0.0;
            bool all_pass3 = true;

            for (int i = 0; i < ITERATIONS; i++) {
                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer tb = mac3.process_tx({ch4, ch5});
                auto t1 = std::chrono::high_resolution_clock::now();
                auto tagged = mac3.process_rx_multi(tb);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx3 += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx3 += std::chrono::duration<double, std::micro>(t2 - t1).count();

                if (i == 0) {
                    if (tagged.size() != 2) { all_pass3 = false; continue; }
                    if (!buffers_equal(tagged[0].second, sdu4)) all_pass3 = false;
                    if (!buffers_equal(tagged[1].second, sdu5)) all_pass3 = false;
                }
            }

            size_t overhead3 = (half > 255 ? 3 : 2) + ((pkt_size - half) > 255 ? 3 : 2);
            std::cout << std::left
                      << std::setw(12) << pkt_size
                      << std::setw(16) << "LCP-On"
                      << std::setw(14) << std::fixed << std::setprecision(2)
                      << (total_tx3 / ITERATIONS)
                      << std::setw(14) << (total_rx3 / ITERATIONS)
                      << std::setw(14) << overhead3
                      << std::setw(10) << (all_pass3 ? "PASS" : "FAIL")
                      << std::endl;
        }

        {
            Config cfg4;
            cfg4.transport_block_size = (pkt_size + 3 > 2048) ? pkt_size + 64 : 2048;
            cfg4.bsr_enabled          = true;
            MacLayer mac4(cfg4);

            ByteBuffer input4 = make_test_sdu(pkt_size, 0xAB);

            double total_tx4 = 0.0, total_rx4 = 0.0;
            bool all_pass4 = true;

            for (int i = 0; i < ITERATIONS; i++) {
                auto t0 = std::chrono::high_resolution_clock::now();
                ByteBuffer tb = mac4.process_tx({input4});
                auto t1 = std::chrono::high_resolution_clock::now();
                auto sdus = mac4.process_rx(tb);
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx4 += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx4 += std::chrono::duration<double, std::micro>(t2 - t1).count();

                if (i == 0 && (sdus.empty() || !buffers_equal(sdus[0], input4)))
                    all_pass4 = false;
            }

            size_t overhead4 = 2 + ((pkt_size > 255) ? 3 : 2);

            std::cout << std::left
                      << std::setw(12) << pkt_size
                      << std::setw(16) << "BSR-On"
                      << std::setw(14) << std::fixed << std::setprecision(2)
                      << (total_tx4 / ITERATIONS)
                      << std::setw(14) << (total_rx4 / ITERATIONS)
                      << std::setw(14) << overhead4
                      << std::setw(10) << (all_pass4 ? "PASS" : "FAIL")
                      << std::endl;
        }
    }
    std::cout << std::endl;
}

static void test_bsr_in_pdu() {
    TEST("BSR CE (LCID=61) present in TB when bsr_enabled=true");

    Config cfg;
    cfg.transport_block_size = 512;
    cfg.bsr_enabled          = true;
    MacLayer mac(cfg);

    ByteBuffer sdu = make_test_sdu(50, 0xAA);
    ByteBuffer tb  = mac.process_tx({sdu});

    if ((tb.data[0] & 0x3F) != 61) {
        FAIL("Expected LCID=61 at byte 0, got " + std::to_string(tb.data[0] & 0x3F));
        return;
    }

    auto recovered = mac.process_rx(tb);
    if (recovered.size() != 1 || !buffers_equal(sdu, recovered[0])) {
        FAIL("SDU round-trip failed with BSR enabled");
        return;
    }

    PASS();
}

static void test_variable_tb_size() {
    TEST("Variable TB size override (process_tx with tb_size=200)");

    Config cfg;
    MacLayer mac(cfg);

    ByteBuffer sdu = make_test_sdu(100, 0xBB);
    ByteBuffer tb  = mac.process_tx({sdu}, 200);

    if (tb.size() != 200) {
        FAIL("Expected TB size 200, got " + std::to_string(tb.size()));
        return;
    }

    auto recovered = mac.process_rx(tb);
    if (recovered.size() != 1 || !buffers_equal(sdu, recovered[0])) {
        FAIL("SDU round-trip failed with variable TB size");
        return;
    }

    PASS();
}

static void test_tb_too_small_truncation() {
    TEST("Truncation: TB too small for SDU (tb_size=10, SDU=100B)");

    Config cfg;
    MacLayer mac(cfg);

    ByteBuffer sdu = make_test_sdu(100, 0xCC);
    ByteBuffer tb  = mac.process_tx({sdu}, 10);

    if (tb.size() != 10) {
        FAIL("Expected TB size 10, got " + std::to_string(tb.size()));
        return;
    }

    auto recovered = mac.process_rx(tb);
    if (!recovered.empty()) {
        FAIL("Expected 0 SDUs from truncated TB, got " + std::to_string(recovered.size()));
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

    test_multi_lcid_mux_demux();
    test_lcp_priority_ordering();
    test_lcp_pbr_quota();
    test_lcp_3channel_roundrobin();

    test_bsr_in_pdu();
    test_variable_tb_size();
    test_tb_too_small_truncation();

    std::cout << "\n  " << tests_passed << " / " << tests_run << " tests passed\n";

    profile_variants();

    return (tests_passed == tests_run) ? 0 : 1;
}
