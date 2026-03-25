// ============================================================
// ip_generator.cpp — Dummy IPv4 packet generator / sink
// ============================================================

#include "ip_generator.h"
#include <iostream>
#include <cstdlib>

IpGenerator::IpGenerator(const Config& cfg) : config_(cfg) {}

void IpGenerator::set_variable_sizes(const std::vector<uint32_t>& sizes) {
    variable_sizes_ = sizes;
}

void IpGenerator::set_payload_pattern(PayloadPattern pattern) {
    payload_pattern_ = pattern;
}

ByteBuffer IpGenerator::generate_packet(uint32_t seq_num) {
    // Determine packet size: use variable_sizes if set, otherwise config
    uint32_t total_size = config_.ip_packet_size;
    if (!variable_sizes_.empty()) {
        total_size = variable_sizes_[seq_num % variable_sizes_.size()];
    }

    // Minimum size: IPv4 header (20) + UDP header (8) = 28 bytes
    if (total_size < 28) {
        total_size = 28;
    }

    ByteBuffer pkt;
    pkt.data.resize(total_size, 0);

    // ---- Build a simplified IPv4 header (20 bytes) ----
    // Byte 0: Version (4) + IHL (5) = 0x45
    pkt.data[0] = 0x45;
    // Byte 1: DSCP / ECN = 0
    pkt.data[1] = 0x00;
    // Bytes 2-3: Total Length (big-endian)
    pkt.data[2] = static_cast<uint8_t>((total_size >> 8) & 0xFF);
    pkt.data[3] = static_cast<uint8_t>(total_size & 0xFF);
    // Bytes 4-5: Identification = sequence number (big-endian)
    pkt.data[4] = static_cast<uint8_t>((seq_num >> 8) & 0xFF);
    pkt.data[5] = static_cast<uint8_t>(seq_num & 0xFF);
    // Bytes 6-7: Flags (Don't Fragment) + Fragment Offset = 0x4000
    pkt.data[6] = 0x40;
    pkt.data[7] = 0x00;
    // Byte 8: TTL = 64
    pkt.data[8] = 64;
    // Byte 9: Protocol = 17 (UDP)
    pkt.data[9] = 17;
    // Bytes 10-11: Header checksum = 0 (skipped for simplicity)
    pkt.data[10] = 0x00;
    pkt.data[11] = 0x00;
    // Bytes 12-15: Source IP = 10.0.0.1
    pkt.data[12] = 10; pkt.data[13] = 0; pkt.data[14] = 0; pkt.data[15] = 1;
    // Bytes 16-19: Destination IP = 10.0.0.2
    pkt.data[16] = 10; pkt.data[17] = 0; pkt.data[18] = 0; pkt.data[19] = 2;

    // ---- Build UDP header (8 bytes, offset 20-27) ----
    uint32_t udp_length = total_size - 20;  // UDP length includes header + data
    // Bytes 20-21: Source port = 5000 (big-endian)
    pkt.data[20] = 0x13;
    pkt.data[21] = 0x88;
    // Bytes 22-23: Destination port = 6000 (big-endian)
    pkt.data[22] = 0x17;
    pkt.data[23] = 0x70;
    // Bytes 24-25: UDP length (big-endian)
    pkt.data[24] = static_cast<uint8_t>((udp_length >> 8) & 0xFF);
    pkt.data[25] = static_cast<uint8_t>(udp_length & 0xFF);
    // Bytes 26-27: UDP checksum = 0 (optional for IPv4)
    pkt.data[26] = 0x00;
    pkt.data[27] = 0x00;

    // ---- Fill payload (starting at byte 28) with selected pattern ----
    switch (payload_pattern_) {
        case PayloadPattern::SEQUENTIAL:
            // Pattern: each byte = (seq_num + byte_index) mod 256
            for (uint32_t i = 28; i < total_size; i++) {
                pkt.data[i] = static_cast<uint8_t>((seq_num + i) & 0xFF);
            }
            break;

        case PayloadPattern::RANDOM:
            // Pseudo-random based on seq_num as seed
            {
                uint32_t seed = seq_num * 0x9E3779B9;  // Golden ratio multiplier
                for (uint32_t i = 28; i < total_size; i++) {
                    seed = seed * 1103515245 + 12345;  // LCG
                    pkt.data[i] = static_cast<uint8_t>((seed >> 16) & 0xFF);
                }
            }
            break;

        case PayloadPattern::ALL_ZEROS:
            // Already zeroed by resize
            break;

        case PayloadPattern::ALL_ONES:
            for (uint32_t i = 28; i < total_size; i++) {
                pkt.data[i] = 0xFF;
            }
            break;
    }

    return pkt;
}

bool IpGenerator::verify_packet(const ByteBuffer& original, const ByteBuffer& recovered) {
    if (original.size() != recovered.size()) {
        std::cerr << "  [VERIFY] Size mismatch: original=" << original.size()
                  << " recovered=" << recovered.size() << "\n";
        return false;
    }
    for (size_t i = 0; i < original.size(); i++) {
        if (original.data[i] != recovered.data[i]) {
            std::cerr << "  [VERIFY] Byte mismatch at offset " << i
                      << ": original=0x" << std::hex << (int)original.data[i]
                      << " recovered=0x" << (int)recovered.data[i] << std::dec << "\n";
            return false;
        }
    }
    return true;
}
