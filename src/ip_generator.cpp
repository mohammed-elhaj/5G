// ============================================================
// ip_generator.cpp — Dummy IPv4 packet generator / sink
// ============================================================

#include "ip_generator.h"
#include <iostream>

IpGenerator::IpGenerator(const Config& cfg) : config_(cfg) {}

ByteBuffer IpGenerator::generate_packet(uint32_t seq_num) {
    // Total packet size = IPv4 header (20 bytes) + payload
    uint32_t total_size = config_.ip_packet_size;
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

    // ---- Fill payload with a repeating pattern based on seq_num ----
    // Pattern: each byte = (seq_num + byte_index) mod 256
    // This is deterministic and easy to verify on the receive side.
    for (uint32_t i = 20; i < total_size; i++) {
        pkt.data[i] = static_cast<uint8_t>((seq_num + i) & 0xFF);
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
