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
    // Member 2: Simplified ROHC-style header compression
    //
    // Stores static IPv4 fields (src/dst IP, protocol, TTL, etc.)
    // from the first packet. Subsequent packets carry only the
    // dynamic fields (Total Length, Identification, Checksum).
    // ========================================================
    struct CompressionContext {
        bool     context_established = false;
        uint8_t  version_ihl    = 0;     // IPv4 byte 0
        uint8_t  dscp_ecn       = 0;     // IPv4 byte 1
        uint16_t flags_fragment = 0;     // IPv4 bytes 6-7
        uint8_t  ttl            = 0;     // IPv4 byte 8
        uint8_t  protocol       = 0;     // IPv4 byte 9
        uint32_t src_ip         = 0;     // IPv4 bytes 12-15
        uint32_t dst_ip         = 0;     // IPv4 bytes 16-19
    };

    CompressionContext tx_comp_ctx_;   // Compressor context (TX side)
    CompressionContext rx_comp_ctx_;   // Decompressor context (RX side)

    static constexpr uint8_t COMPRESSED_MARKER = 0xFC;
    static constexpr size_t  IPV4_HEADER_SIZE  = 20;
    static constexpr size_t  COMPRESSED_HEADER_SIZE = 7;  // 1 marker + 2 length + 2 id + 2 checksum


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
    //   - Replaces 20-byte IPv4 header with 7-byte compressed header
    //     [0xFC marker][Total Length(2B)][Identification(2B)][Checksum(2B)]
    //
    // decompress_header:
    //   - Restores full 20-byte IPv4 header from context + dynamic fields
    // ========================================================
    std::vector<uint8_t> compress_header(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompress_header(const std::vector<uint8_t>& data);
};
