#pragma once
// ============================================================
// pdcp.h — PDCP Layer (TS 38.323, simplified)
//
// Uplink TX path:
//   IP packet → assign SN → cipher → compute MAC-I → PDCP PDU
//
// Downlink RX path:
//   PDCP PDU → parse header → verify MAC-I → decipher → IP packet
//
// Ciphering:  XOR-based stream cipher (simplified, fully reversible)
// Integrity:  CRC32-based MAC-I (4 bytes)
// ============================================================

#include "common.h"
#include <vector>

class PdcpLayer {
public:
    explicit PdcpLayer(const Config& cfg);

    /// Uplink TX: accept an IP packet (SDU), produce a PDCP PDU.
    ByteBuffer process_tx(const ByteBuffer& sdu);

    /// Downlink RX: accept a PDCP PDU, recover the original IP packet (SDU).
    ByteBuffer process_rx(const ByteBuffer& pdu);

    /// Reset sequence number counters (e.g., between test runs).
    void reset();

private:
    uint32_t tx_next_ = 0;   // Next COUNT / SN to assign on TX
    uint32_t rx_next_ = 0;   // Next expected COUNT on RX
    Config   config_;

    /// Generate a pseudo-keystream of the given length from (cipher_key, count).
    /// keystream[i] = cipher_key[i % 16] ^ (count + i)
    std::vector<uint8_t> generate_keystream(uint32_t count, size_t length);

    /// XOR data in-place with the keystream derived from count.
    void apply_cipher(std::vector<uint8_t>& data, uint32_t count);

    /// Compute a 4-byte MAC-I using CRC32 over (integrity_key + count + data).
    uint32_t compute_mac_i(uint32_t count, const std::vector<uint8_t>& data);
};
