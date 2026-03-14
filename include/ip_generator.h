#pragma once
// ============================================================
// ip_generator.h — Dummy IPv4 packet generator / sink
//
// Generates fake IP packets with a simplified 20-byte IPv4
// header and a repeating payload pattern.  Also acts as the
// verification sink on the downlink side: compares recovered
// packets byte-for-byte against the originals.
// ============================================================

#include "common.h"

class IpGenerator {
public:
    explicit IpGenerator(const Config& cfg);

    /// Generate a dummy IP packet for the given sequence number.
    /// The payload is filled with a deterministic pattern so that
    /// the receiver can verify every byte.
    ByteBuffer generate_packet(uint32_t seq_num);

    /// Compare the recovered packet to the original byte-for-byte.
    /// Returns true if they match exactly.
    bool verify_packet(const ByteBuffer& original, const ByteBuffer& recovered);

private:
    Config config_;
};
