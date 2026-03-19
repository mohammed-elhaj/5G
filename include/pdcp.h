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

// ============================================================
// PDCP Layer (extended with header compression)
//
// Member 2 contribution:
//   - Added ROHC-style header compression support
//   - Maintains TX/RX compression state
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

    // ========================================================
    // Member 2: Compression state
    //
    // These variables maintain context required for
    // ROHC-style delta compression.
    // ========================================================
    uint16_t comp_tx_id_ = 0;   // Packet ID for TX compressed packets
    uint16_t comp_rx_id_ = 0;   // Packet ID for RX (for tracking/debugging)

    uint8_t prev_tx_len_ = 0;   // Previous TX packet length (for delta encoding)
    uint8_t prev_rx_len_ = 0;   // Previous RX packet length

    // ========================================================


    /// Generate a pseudo-keystream of the given length from (cipher_key, count).
    /// keystream[i] = cipher_key[i % 16] ^ (count + i)
    std::vector<uint8_t> generate_keystream(uint32_t count, size_t length);

    /// XOR data in-place with the keystream derived from count.
    void apply_cipher(std::vector<uint8_t>& data, uint32_t count);

    /// Compute a 4-byte MAC-I using CRC32 over (integrity_key + count + data).
    uint32_t compute_mac_i(uint32_t count, const std::vector<uint8_t>& data);

    // --- Member 1: AES-128-CTR cipher (NEA2 simplified) ---
    void apply_cipher_aes_ctr(std::vector<uint8_t>& data, uint32_t count);

    // --- Member 1: HMAC-SHA256 integrity ---
    uint32_t compute_mac_i_hmac(uint32_t count, const std::vector<uint8_t>& data);

    // ========================================================
    // Member 2: Header compression functions
    //
    // compress_header:
    //   - Replaces 20-byte IPv4 header with 4-byte compact header
    //
    // decompress_header:
    //   - Restores original IPv4 header using stored context
    // ========================================================
    std::vector<uint8_t> compress_header(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompress_header(const std::vector<uint8_t>& data);
    std::vector<uint8_t> stored_ip_header_;
    bool context_initialized_ = false;
};
