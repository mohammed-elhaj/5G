#pragma once
// ============================================================
// ip_generator.h — Dummy IPv4 packet generator / sink
//
// Generates fake IP packets with a simplified 20-byte IPv4
// header, 8-byte UDP header, and various payload patterns.
// Also acts as the verification sink on the downlink side:
// compares recovered packets byte-for-byte against the originals.
// ============================================================

#include "common.h"
#include <vector>

enum class PayloadPattern {
    SEQUENTIAL,  // (seq_num + byte_index) mod 256 (default)
    RANDOM,      // Pseudo-random bytes based on seq_num seed
    ALL_ZEROS,   // All 0x00
    ALL_ONES     // All 0xFF
};

class IpGenerator {
public:
    explicit IpGenerator(const Config& cfg);

    /// Generate a dummy IP packet for the given sequence number.
    /// If variable_sizes is set, uses the size from that vector (cycling).
    /// The payload is filled with a deterministic pattern so that
    /// the receiver can verify every byte.
    ByteBuffer generate_packet(uint32_t seq_num);

    /// Compare the recovered packet to the original byte-for-byte.
    /// Returns true if they match exactly.
    bool verify_packet(const ByteBuffer& original, const ByteBuffer& recovered);

    /// Set variable packet sizes (cycles through the list)
    void set_variable_sizes(const std::vector<uint32_t>& sizes);

    /// Set payload pattern
    void set_payload_pattern(PayloadPattern pattern);

private:
    Config config_;
    std::vector<uint32_t> variable_sizes_;
    PayloadPattern payload_pattern_ = PayloadPattern::SEQUENTIAL;
};
