// ============================================================
// main.cpp — 5G NR Layer 2 Protocol Stack Simulator
//
// Transport Block loopback test:
//   For each IP packet:
//     Uplink:   IP → PDCP TX → RLC TX → MAC TX → Transport Block
//     Downlink: Transport Block → MAC RX → RLC RX → PDCP RX → IP
//     Verify:   recovered IP == original IP
//
// Per-layer timing is collected using std::chrono and written
// to profiling_results.csv for analysis.
//
// Command-line options:
//   --packet-size <N>   IP packet size in bytes (default: 1400)
//   --num-packets <N>   Number of packets to process (default: 10)
//   --tb-size <N>       Transport Block size in bytes (default: 2048)
// ============================================================

#include "common.h"
#include "ip_generator.h"
#include "pdcp.h"
#include "rlc.h"
#include "mac.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <string>
#include <vector>
#include <cstring>

// ============================================================
// Simple command-line parser
// ============================================================
static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--packet-size") == 0 && i + 1 < argc) {
            cfg.ip_packet_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--num-packets") == 0 && i + 1 < argc) {
            cfg.num_packets = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--tb-size") == 0 && i + 1 < argc) {
            cfg.transport_block_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            std::cerr << "Usage: 5g_layer2 [--packet-size N] [--num-packets N] [--tb-size N]\n";
        }
    }
    return cfg;
}

// ============================================================
// Timing helper — returns elapsed microseconds
// ============================================================
using Clock = std::chrono::high_resolution_clock;

static double elapsed_us(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

// ============================================================
// Per-packet profiling record
// ============================================================
struct PacketProfile {
    uint32_t seq;
    double pdcp_tx_us;
    double rlc_tx_us;
    double mac_tx_us;
    double mac_rx_us;
    double rlc_rx_us;
    double pdcp_rx_us;
    bool   pass;
};

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);
    // AI-assisted: Member 6 - Check if variable TB size mode is enabled
    bool use_variable_tb = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--variable-tb") == 0) {
            use_variable_tb = true;
        }
    }

    std::cout << "========================================\n";
    std::cout << " 5G NR Layer 2 Protocol Stack Simulator\n";
    std::cout << "========================================\n";
    std::cout << "  IP packet size   : " << cfg.ip_packet_size << " bytes\n";
    std::cout << "  Number of packets: " << cfg.num_packets << "\n";
    std::cout << "  Transport Block  : " << cfg.transport_block_size << " bytes\n";
    std::cout << "  PDCP SN length   : " << (int)cfg.pdcp_sn_length << " bits\n";
    std::cout << "  RLC mode         : UM (6-bit SN)\n";
    std::cout << "  RLC max PDU size : " << cfg.rlc_max_pdu_size << " bytes\n";
    std::cout << "  Ciphering        : " << (cfg.ciphering_enabled ? "ON" : "OFF") << "\n";
    std::cout << "  Integrity        : " << (cfg.integrity_enabled ? "ON" : "OFF") << "\n";
    std::cout << "========================================\n\n";

    // Create layer instances
    IpGenerator ip_gen(cfg);
    PdcpLayer   pdcp(cfg);
    RlcLayer    rlc(cfg);
    MacLayer    mac(cfg);

    std::vector<PacketProfile> profiles;
    uint32_t pass_count = 0;
    uint32_t fail_count = 0;

    for (uint32_t seq = 0; seq < cfg.num_packets; seq++) {
        PacketProfile pp{};
        pp.seq = seq;

        // ---- Generate the original IP packet ----
        ByteBuffer original_ip = ip_gen.generate_packet(seq);

        // ============================================================
        // UPLINK PATH: IP → PDCP TX → RLC TX → MAC TX
        // ============================================================

        // -- PDCP TX --
        auto t0 = Clock::now();
        ByteBuffer pdcp_pdu = pdcp.process_tx(original_ip);
        auto t1 = Clock::now();
        pp.pdcp_tx_us = elapsed_us(t0, t1);

        // -- RLC TX (may produce multiple PDUs via segmentation) --
        t0 = Clock::now();
        std::vector<ByteBuffer> rlc_pdus = rlc.process_tx(pdcp_pdu);
        t1 = Clock::now();
        pp.rlc_tx_us = elapsed_us(t0, t1);

        // -- MAC TX (multiplex all RLC PDUs into one Transport Block) --
        t0 = Clock::now();
        // AI-assisted: Member 6 - Variable TB logic (cycle: 512, 1024, 2048)
        size_t current_tb_size = cfg.transport_block_size;
        if (use_variable_tb) {
            static size_t sizes[] = {512, 1024, 2048};
            static int cycle_count = 0;
            current_tb_size = sizes[(cycle_count++) % 3];
        }
        ByteBuffer transport_block = mac.process_tx(rlc_pdus, current_tb_size);
        t1 = Clock::now();
        pp.mac_tx_us = elapsed_us(t0, t1);

        // ============================================================
        // LOOPBACK — the Transport Block goes directly to the RX side
        // ============================================================

        // ============================================================
        // DOWNLINK PATH: MAC RX → RLC RX → PDCP RX
        // ============================================================

        // -- MAC RX (demux the Transport Block into MAC SDUs = RLC PDUs) --
        t0 = Clock::now();
        std::vector<ByteBuffer> rx_rlc_pdus = mac.process_rx(transport_block);
        t1 = Clock::now();
        pp.mac_rx_us = elapsed_us(t0, t1);

        // -- RLC RX (reassemble segments into complete PDCP PDU) --
        t0 = Clock::now();
        ByteBuffer recovered_pdcp_pdu;
        for (const auto& rpdu : rx_rlc_pdus) {
            auto reassembled = rlc.process_rx(rpdu);
            for (auto& sdu : reassembled) {
                // In V1 we expect exactly one reassembled SDU per packet
                recovered_pdcp_pdu = std::move(sdu);
            }
        }
        t1 = Clock::now();
        pp.rlc_rx_us = elapsed_us(t0, t1);

        // -- PDCP RX (decipher + verify integrity → recover IP packet) --
        t0 = Clock::now();
        ByteBuffer recovered_ip = pdcp.process_rx(recovered_pdcp_pdu);
        t1 = Clock::now();
        pp.pdcp_rx_us = elapsed_us(t0, t1);

        // ============================================================
        // VERIFICATION
        // ============================================================
        pp.pass = ip_gen.verify_packet(original_ip, recovered_ip);

        if (pp.pass) {
            pass_count++;
            std::cout << "  Packet " << std::setw(4) << seq << ": PASS";
        } else {
            fail_count++;
            std::cout << "  Packet " << std::setw(4) << seq << ": FAIL";
        }

        // Print per-packet timing summary
        double total_ul = pp.pdcp_tx_us + pp.rlc_tx_us + pp.mac_tx_us;
        double total_dl = pp.mac_rx_us  + pp.rlc_rx_us  + pp.pdcp_rx_us;
        std::cout << "  |  UL: " << std::fixed << std::setprecision(1)
                  << total_ul << " us  DL: " << total_dl << " us"
                  << "  |  RLC segments: " << rlc_pdus.size() << "\n";

        profiles.push_back(pp);
    }

    // ============================================================
    // SUMMARY
    // ============================================================
    std::cout << "\n========================================\n";
    std::cout << " Results: " << pass_count << " PASS, " << fail_count << " FAIL"
              << " out of " << cfg.num_packets << " packets\n";
    std::cout << "========================================\n";

    // Compute averages
    double avg_pdcp_tx = 0, avg_rlc_tx = 0, avg_mac_tx = 0;
    double avg_mac_rx = 0,  avg_rlc_rx = 0, avg_pdcp_rx = 0;
    for (auto& p : profiles) {
        avg_pdcp_tx += p.pdcp_tx_us;
        avg_rlc_tx  += p.rlc_tx_us;
        avg_mac_tx  += p.mac_tx_us;
        avg_mac_rx  += p.mac_rx_us;
        avg_rlc_rx  += p.rlc_rx_us;
        avg_pdcp_rx += p.pdcp_rx_us;
    }
    double n = static_cast<double>(profiles.size());
    avg_pdcp_tx /= n; avg_rlc_tx /= n; avg_mac_tx /= n;
    avg_mac_rx  /= n; avg_rlc_rx /= n; avg_pdcp_rx /= n;

    std::cout << "\n  Average per-layer timing (microseconds):\n";
    std::cout << "  +-----------+----------+----------+\n";
    std::cout << "  | Layer     | Uplink   | Downlink |\n";
    std::cout << "  +-----------+----------+----------+\n";
    std::cout << "  | PDCP      | " << std::setw(8) << std::fixed << std::setprecision(2)
              << avg_pdcp_tx << " | " << std::setw(8) << avg_pdcp_rx << " |\n";
    std::cout << "  | RLC       | " << std::setw(8) << avg_rlc_tx << " | " << std::setw(8)
              << avg_rlc_rx << " |\n";
    std::cout << "  | MAC       | " << std::setw(8) << avg_mac_tx << " | " << std::setw(8)
              << avg_mac_rx << " |\n";
    std::cout << "  +-----------+----------+----------+\n";
    std::cout << "  | TOTAL     | " << std::setw(8) << (avg_pdcp_tx + avg_rlc_tx + avg_mac_tx)
              << " | " << std::setw(8) << (avg_mac_rx + avg_rlc_rx + avg_pdcp_rx) << " |\n";
    std::cout << "  +-----------+----------+----------+\n";

    // ============================================================
    // Write profiling CSV
    // ============================================================
    std::ofstream csv("profiling_results.csv");
    if (csv.is_open()) {
        csv << "seq,pdcp_tx_us,rlc_tx_us,mac_tx_us,mac_rx_us,rlc_rx_us,pdcp_rx_us,pass\n";
        for (auto& p : profiles) {
            csv << p.seq << ","
                << std::fixed << std::setprecision(3)
                << p.pdcp_tx_us << ","
                << p.rlc_tx_us  << ","
                << p.mac_tx_us  << ","
                << p.mac_rx_us  << ","
                << p.rlc_rx_us  << ","
                << p.pdcp_rx_us << ","
                << (p.pass ? "PASS" : "FAIL") << "\n";
        }
        csv.close();
        std::cout << "\n  Profiling data written to profiling_results.csv\n";
    } else {
        std::cerr << "  WARNING: Could not open profiling_results.csv for writing\n";
    }

    return (fail_count == 0) ? 0 : 1;
}
