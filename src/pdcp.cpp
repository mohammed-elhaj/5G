// ============================================================
// pdcp.cpp — PDCP Layer implementation (TS 38.323 simplified)
//
// PDU format for 12-bit SN (DRBs):
//   Byte 0:     [D/C=1 (1 bit)] [Reserved=000 (3 bits)] [SN bits 11..8 (4 bits)]
//   Byte 1:     [SN bits 7..0]
//   Byte 2..N:  Ciphered payload
//   Byte N+1..N+4: MAC-I (4 bytes, if integrity enabled)
//
// PDU format for 18-bit SN:
//   Byte 0:     [D/C=1 (1 bit)] [R=0 (1 bit)] [SN bits 17..12 (6 bits)]
//   Byte 1:     [SN bits 11..4]
//   Byte 2:     [SN bits 3..0 (4 bits)] [Reserved=0000 (4 bits)]
//   Byte 3..N:  Ciphered payload
//   Byte N+1..N+4: MAC-I
// ============================================================

#include "pdcp.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

// ============================================================
// CRC32 lookup table (ISO 3309 / ITU-T V.42 polynomial 0xEDB88320)
// Used to produce the 4-byte MAC-I integrity tag.
// ============================================================
static uint32_t crc32_table[256];
static bool     crc32_table_ready = false;

static void build_crc32_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
        crc32_table[i] = crc;
    }
    crc32_table_ready = true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    if (!crc32_table_ready) build_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================
// Constructor / reset
// ============================================================
PdcpLayer::PdcpLayer(const Config& cfg) : config_(cfg) {}

void PdcpLayer::reset() {
    tx_next_ = 0;
    rx_next_ = 0;
}

// ============================================================
// Keystream generation — simplified XOR-based stream cipher.
// For each byte position i:  keystream[i] = cipher_key[i%16] ^ (count + i)
// This is NOT cryptographically secure but is fully reversible,
// which is exactly what we need for the loopback test.
// ============================================================
std::vector<uint8_t> PdcpLayer::generate_keystream(uint32_t count, size_t length) {
    std::vector<uint8_t> ks(length);
    for (size_t i = 0; i < length; i++) {
        ks[i] = config_.cipher_key[i % 16] ^ static_cast<uint8_t>((count + i) & 0xFF);
    }
    return ks;
}

// ============================================================
// Apply cipher — XOR the data buffer with the keystream in-place.
// Because XOR is its own inverse, the same function ciphers and
// deciphers.
// ============================================================
void PdcpLayer::apply_cipher(std::vector<uint8_t>& data, uint32_t count) {
    auto ks = generate_keystream(count, data.size());
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= ks[i];
    }
}

// ============================================================
// Compute MAC-I — CRC32 over (integrity_key || count || data).
// Produces a 4-byte integrity check value.
// ============================================================
uint32_t PdcpLayer::compute_mac_i(uint32_t count, const std::vector<uint8_t>& data) {
    // Build the input buffer: integrity_key (16 bytes) + count (4 bytes) + data
    std::vector<uint8_t> buf;
    buf.reserve(16 + 4 + data.size());

    // Append the 16-byte integrity key
    buf.insert(buf.end(), config_.integrity_key, config_.integrity_key + 16);

    // Append count in big-endian
    buf.push_back(static_cast<uint8_t>((count >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((count >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((count >> 8)  & 0xFF));
    buf.push_back(static_cast<uint8_t>(count & 0xFF));

    // Append the payload data
    buf.insert(buf.end(), data.begin(), data.end());

    return crc32(buf.data(), buf.size());
}

// ============================================================
// TX path (uplink): SDU (IP packet) → PDCP PDU
//
// Steps per TS 38.323 §5.7 / §5.8 (simplified):
//   1. Assign a COUNT (= tx_next_) and derive the SN
//   2. (Optional) Header compression — skipped in V1
//   3. Cipher the payload using the XOR keystream
//   4. Compute the integrity MAC-I over the ciphered payload
//   5. Build the PDCP PDU: [header | ciphered payload | MAC-I]
//   6. Increment tx_next_
// ============================================================
ByteBuffer PdcpLayer::process_tx(const ByteBuffer& sdu) {
    uint32_t count = tx_next_++;
    uint32_t sn    = count;   // SN = COUNT mod (2^sn_length), but within V1 range this is fine

    // --- Step 1: Determine header size based on SN length ---
    size_t header_size = (config_.pdcp_sn_length == 12) ? 2 : 3;

    // --- Step 2: Copy the SDU payload (the IP packet) ---
    std::vector<uint8_t> payload(sdu.data.begin(), sdu.data.end());

    // --- Step 3: Cipher the payload ---
    if (config_.ciphering_enabled) {
        apply_cipher(payload, count);
    }

    // --- Step 4: Compute MAC-I over the ciphered payload ---
    uint32_t mac_i = 0;
    if (config_.integrity_enabled) {
        mac_i = compute_mac_i(count, payload);
    }

    // --- Step 5: Assemble the PDCP PDU ---
    ByteBuffer pdu;
    size_t mac_i_size = config_.integrity_enabled ? 4 : 0;
    pdu.data.resize(header_size + payload.size() + mac_i_size);

    // Write the PDCP header
    if (config_.pdcp_sn_length == 12) {
        // 12-bit SN format:
        //   Byte 0: [D/C=1][R=0][R=0][R=0][SN 11..8]
        //   Byte 1: [SN 7..0]
        pdu.data[0] = 0x80 | static_cast<uint8_t>((sn >> 8) & 0x0F);  // D/C=1 in bit 7
        pdu.data[1] = static_cast<uint8_t>(sn & 0xFF);
    } else {
        // 18-bit SN format:
        //   Byte 0: [D/C=1][R=0][SN 17..12]
        //   Byte 1: [SN 11..4]
        //   Byte 2: [SN 3..0][R=0000]
        pdu.data[0] = 0x80 | static_cast<uint8_t>((sn >> 12) & 0x3F);
        pdu.data[1] = static_cast<uint8_t>((sn >> 4) & 0xFF);
        pdu.data[2] = static_cast<uint8_t>((sn & 0x0F) << 4);
    }

    // Copy ciphered payload after the header
    std::memcpy(pdu.data.data() + header_size, payload.data(), payload.size());

    // Append MAC-I (4 bytes, big-endian)
    if (config_.integrity_enabled) {
        size_t offset = header_size + payload.size();
        pdu.data[offset + 0] = static_cast<uint8_t>((mac_i >> 24) & 0xFF);
        pdu.data[offset + 1] = static_cast<uint8_t>((mac_i >> 16) & 0xFF);
        pdu.data[offset + 2] = static_cast<uint8_t>((mac_i >> 8)  & 0xFF);
        pdu.data[offset + 3] = static_cast<uint8_t>(mac_i & 0xFF);
    }

    return pdu;
}

// ============================================================
// RX path (downlink): PDCP PDU → recovered SDU (IP packet)
//
// Steps (reverse of TX):
//   1. Parse the PDCP header to extract SN
//   2. Reconstruct the COUNT value
//   3. Extract the ciphered payload and MAC-I
//   4. Verify the MAC-I integrity tag
//   5. Decipher the payload (same XOR operation as cipher)
//   6. Increment rx_next_
//   7. Return the recovered IP packet
// ============================================================
ByteBuffer PdcpLayer::process_rx(const ByteBuffer& pdu) {
    // --- Step 1: Determine header size and extract SN ---
    size_t header_size = (config_.pdcp_sn_length == 12) ? 2 : 3;
    uint32_t sn = 0;

    if (config_.pdcp_sn_length == 12) {
        // 12-bit SN: Byte 0 lower 4 bits = SN[11..8], Byte 1 = SN[7..0]
        sn = (static_cast<uint32_t>(pdu.data[0] & 0x0F) << 8) | pdu.data[1];
    } else {
        // 18-bit SN: Byte 0 lower 6 bits = SN[17..12], Byte 1 = SN[11..4], Byte 2 upper 4 bits = SN[3..0]
        sn = (static_cast<uint32_t>(pdu.data[0] & 0x3F) << 12) |
             (static_cast<uint32_t>(pdu.data[1]) << 4) |
             (static_cast<uint32_t>(pdu.data[2] >> 4));
    }

    // --- Step 2: Reconstruct COUNT ---
    // In V1 we process packets in order, so COUNT = rx_next_.
    uint32_t count = rx_next_++;

    // --- Step 3: Extract ciphered payload and MAC-I ---
    size_t mac_i_size    = config_.integrity_enabled ? 4 : 0;
    size_t payload_size  = pdu.size() - header_size - mac_i_size;

    std::vector<uint8_t> payload(pdu.data.begin() + header_size,
                                  pdu.data.begin() + header_size + payload_size);

    // --- Step 4: Verify MAC-I ---
    if (config_.integrity_enabled) {
        uint32_t expected_mac_i = compute_mac_i(count, payload);

        // Read the received MAC-I (big-endian, last 4 bytes of PDU)
        size_t mac_offset = header_size + payload_size;
        uint32_t received_mac_i =
            (static_cast<uint32_t>(pdu.data[mac_offset + 0]) << 24) |
            (static_cast<uint32_t>(pdu.data[mac_offset + 1]) << 16) |
            (static_cast<uint32_t>(pdu.data[mac_offset + 2]) << 8)  |
            (static_cast<uint32_t>(pdu.data[mac_offset + 3]));

        if (received_mac_i != expected_mac_i) {
            std::cerr << "  [PDCP RX] MAC-I verification FAILED for COUNT=" << count
                      << " (expected=0x" << std::hex << expected_mac_i
                      << " received=0x" << received_mac_i << std::dec << ")\n";
            // Return empty buffer to signal integrity failure
            return ByteBuffer{};
        }
    }

    // --- Step 5: Decipher the payload ---
    // XOR is self-inverse, so apply_cipher deciphers as well.
    if (config_.ciphering_enabled) {
        apply_cipher(payload, count);
    }

    // --- Step 6: Return the recovered SDU ---
    ByteBuffer sdu;
    sdu.data = std::move(payload);
    return sdu;
}
