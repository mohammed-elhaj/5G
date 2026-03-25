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
static Config parse_args(int argc, char* argv[], std::vector<uint32_t>& packet_sizes, bool& stress_mode) {
    Config cfg;
    stress_mode = false;
    
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--packet-size") == 0 && i + 1 < argc) {
            cfg.ip_packet_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--packet-sizes") == 0 && i + 1 < argc) {
            // Parse comma-separated list: --packet-sizes 100,500,1000,1400
            std::string sizes_str = argv[++i];
            size_t pos = 0;
            while (pos < sizes_str.length()) {
                size_t comma = sizes_str.find(',', pos);
                if (comma == std::string::npos) {
                    packet_sizes.push_back(std::stoul(sizes_str.substr(pos)));
                    break;
                } else {
                    packet_sizes.push_back(std::stoul(sizes_str.substr(pos, comma - pos)));
                    pos = comma + 1;
                }
            }
        } else if (std::strcmp(argv[i], "--num-packets") == 0 && i + 1 < argc) {
            cfg.num_packets = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--tb-size") == 0 && i + 1 < argc) {
            cfg.transport_block_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--stress") == 0) {
            stress_mode = true;
            cfg.num_packets = 100;  // Default stress test size
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            std::cerr << "Usage: 5g_layer2 [options]\n";
            std::cerr << "  --packet-size N         Single packet size in bytes\n";
            std::cerr << "  --packet-sizes N,M,...  Variable packet sizes (comma-separated)\n";
            std::cerr << "  --num-packets N         Number of packets to process\n";
            std::cerr << "  --tb-size N             Transport Block size in bytes\n";
            std::cerr << "  --stress                Run stress test (100+ packets)\n";
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
    uint32_t packet_size;
    uint32_t tb_size;
    double pdcp_tx_us;
    double rlc_tx_us;
    double mac_tx_us;
    double mac_rx_us;
    double rlc_rx_us;
    double pdcp_rx_us;
    double total_tx_us;
    double total_rx_us;
    bool   pass;
};

int main(int argc, char* argv[]) {
    std::vector<uint32_t> packet_sizes;
    bool stress_mode = false;
    Config cfg = parse_args(argc, argv, packet_sizes, stress_mode);

    std::cout << "========================================\n";
    std::cout << " 5G NR Layer 2 Protocol Stack Simulator\n";
    std::cout << "========================================\n";
    
    if (!packet_sizes.empty()) {
        std::cout << "  Variable packet sizes: ";
        for (size_t i = 0; i < packet_sizes.size(); i++) {
            std::cout << packet_sizes[i];
            if (i < packet_sizes.size() - 1) std::cout << ", ";
        }
        std::cout << " bytes\n";
    } else {
        std::cout << "  IP packet size   : " << cfg.ip_packet_size << " bytes\n";
    }
    
    std::cout << "  Number of packets: " << cfg.num_packets << "\n";
    if (stress_mode) {
        std::cout << "  Mode             : STRESS TEST\n";
    }
    std::cout << "  Transport Block  : " << cfg.transport_block_size << " bytes\n";
    std::cout << "  PDCP SN length   : " << (int)cfg.pdcp_sn_length << " bits\n";
    std::cout << "  RLC mode         : UM (6-bit SN)\n";
    std::cout << "  RLC max PDU size : " << cfg.rlc_max_pdu_size << " bytes\n";
    std::cout << "  Ciphering        : " << (cfg.ciphering_enabled ? "ON" : "OFF") << "\n";
    std::cout << "  Integrity        : " << (cfg.integrity_enabled ? "ON" : "OFF") << "\n";
    std::cout << "========================================\n\n";

    // Create layer instances
    IpGenerator ip_gen(cfg);
    if (!packet_sizes.empty()) {
        ip_gen.set_variable_sizes(packet_sizes);
    }
    
    PdcpLayer   pdcp(cfg);
    RlcLayer    rlc(cfg);
    MacLayer    mac(cfg);

    std::vector<PacketProfile> profiles;
    uint32_t pass_count = 0;
    uint32_t fail_count = 0;

    for (uint32_t seq = 0; seq < cfg.num_packets; seq++) {
        PacketProfile pp{};
        pp.seq = seq;
        
        // Generate the original IP packet
        ByteBuffer original_ip = ip_gen.generate_packet(seq);
        pp.packet_size = original_ip.size();
        pp.tb_size = cfg.transport_block_size;

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
        ByteBuffer transport_block = mac.process_tx(rlc_pdus);
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

        // Calculate total times
        pp.total_tx_us = pp.pdcp_tx_us + pp.rlc_tx_us + pp.mac_tx_us;
        pp.total_rx_us = pp.mac_rx_us + pp.rlc_rx_us + pp.pdcp_rx_us;

        if (pp.pass) {
            pass_count++;
            std::cout << "  Packet " << std::setw(4) << seq << ": PASS";
        } else {
            fail_count++;
            std::cout << "  Packet " << std::setw(4) << seq << ": FAIL";
        }

        // Print per-packet timing summary
        std::cout << "  |  UL: " << std::fixed << std::setprecision(1)
                  << pp.total_tx_us << " us  DL: " << pp.total_rx_us << " us"
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
        // Enhanced CSV header with packet_size, tb_size, and total times
        csv << "seq,packet_size,tb_size,"
            << "pdcp_tx_us,rlc_tx_us,mac_tx_us,"
            << "mac_rx_us,rlc_rx_us,pdcp_rx_us,"
            << "total_tx_us,total_rx_us,total_us,pass\n";
        
        for (auto& p : profiles) {
            csv << p.seq << ","
                << p.packet_size << ","
                << p.tb_size << ","
                << std::fixed << std::setprecision(3)
                << p.pdcp_tx_us << ","
                << p.rlc_tx_us  << ","
                << p.mac_tx_us  << ","
                << p.mac_rx_us  << ","
                << p.rlc_rx_us  << ","
                << p.pdcp_rx_us << ","
                << p.total_tx_us << ","
                << p.total_rx_us << ","
                << (p.total_tx_us + p.total_rx_us) << ","
                << (p.pass ? "PASS" : "FAIL") << "\n";
        }
        csv.close();
        std::cout << "\n  Profiling data written to profiling_results.csv\n";
    } else {
        std::cerr << "  WARNING: Could not open profiling_results.csv for writing\n";
    }

    return (fail_count == 0) ? 0 : 1;
}
