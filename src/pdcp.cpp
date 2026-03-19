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
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>

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

    comp_tx_id_ = 0;
    comp_rx_id_ = 0;
    prev_tx_len_ = 0;
    prev_rx_len_ = 0;

    context_initialized_ = false;
    stored_ip_header_.clear();
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
    if (config_.cipher_algorithm == 0) {
        // V1: XOR stream cipher (original code, unchanged)
        auto ks = generate_keystream(count, data.size());
        for (size_t i = 0; i < data.size(); i++) {
            data[i] ^= ks[i];
        }
    } else if (config_.cipher_algorithm == 1) {
        // Optimized: AES-128-CTR (per TS 38.323 §5.8, NEA2)
        apply_cipher_aes_ctr(data, count);
    }
}

// ============================================================
// AES-128-CTR cipher — replaces XOR stream cipher when
// config_.cipher_algorithm == 1.
//
// Per TS 38.323 §5.8, NEA2 uses AES-128 in CTR mode.
// IV = COUNT(4 bytes) || BEARER(1 byte) || DIRECTION(1 byte) || zeros(10 bytes)
//
// AES-CTR is self-inverse: same operation encrypts and decrypts.
// AI-assisted: reviewed by Member 1
// ============================================================
void PdcpLayer::apply_cipher_aes_ctr(std::vector<uint8_t>& data, uint32_t count) {
    // Build 16-byte IV per TS 38.323 ciphering input parameters
    uint8_t iv[16] = {0};
    iv[0] = static_cast<uint8_t>((count >> 24) & 0xFF);
    iv[1] = static_cast<uint8_t>((count >> 16) & 0xFF);
    iv[2] = static_cast<uint8_t>((count >> 8) & 0xFF);
    iv[3] = static_cast<uint8_t>(count & 0xFF);
    iv[4] = 0x01;  // BEARER ID (single DRB)
    iv[5] = 0x00;  // DIRECTION (0 for both TX/RX in loopback simulation)

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::cerr << "[PDCP] Failed to create EVP cipher context\n";
        return;
    }

    // AES-128-CTR: same call encrypts and decrypts
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr,
                           config_.cipher_key, iv) != 1) {
        std::cerr << "[PDCP] EVP_EncryptInit_ex failed\n";
        EVP_CIPHER_CTX_free(ctx);
        return;
    }

    std::vector<uint8_t> output(data.size() + EVP_CIPHER_block_size(EVP_aes_128_ctr()));
    int out_len = 0;

    if (EVP_EncryptUpdate(ctx, output.data(), &out_len,
                          data.data(), static_cast<int>(data.size())) != 1) {
        std::cerr << "[PDCP] EVP_EncryptUpdate failed\n";
        EVP_CIPHER_CTX_free(ctx);
        return;
    }

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx, output.data() + out_len, &final_len) != 1) {
        std::cerr << "[PDCP] EVP_EncryptFinal_ex failed\n";
        EVP_CIPHER_CTX_free(ctx);
        return;
    }

    output.resize(out_len + final_len);
    data = std::move(output);

    EVP_CIPHER_CTX_free(ctx);
}

// ============================================================
// Member 2: Header Compression (FINAL VERSION)
//
// Strategy:
//   - First IPv4 packet is sent UNCOMPRESSED to establish context
//   - Its full 20-byte header is stored
//   - Subsequent packets:
//       Replace IPv4 header (20B) with:
//         [0xFC][packet_id(2B)][total_length(2B)]
//
// Result:
//   20B → 5B (safe, no truncation issues)
//
// Only applies to IPv4 packets (Version = 4)
// ============================================================
std::vector<uint8_t> PdcpLayer::compress_header(const std::vector<uint8_t>& data) {

    // --- Ensure packet is large enough ---
    if (data.size() < 20) return data;

    // --- Check IPv4 version ---
    if ((data[0] >> 4) != 4) return data;

    // ========================================================
    // First packet: establish compression context
    // ========================================================
    if (!context_initialized_) {
        stored_ip_header_.assign(data.begin(), data.begin() + 20);
        context_initialized_ = true;

        // Send first packet uncompressed
        return data;
    }

    // ========================================================
    // Compress packet
    // ========================================================
    std::vector<uint8_t> out;

    // --- Marker ---
    out.push_back(0xFC);

    // --- Packet ID (2 bytes) ---
    out.push_back((comp_tx_id_ >> 8) & 0xFF);
    out.push_back(comp_tx_id_ & 0xFF);

    // --- Total length (2 bytes, NO truncation) ---
    uint16_t total_len = static_cast<uint16_t>(data.size());
    out.push_back((total_len >> 8) & 0xFF);
    out.push_back(total_len & 0xFF);

    // --- Append payload (skip 20-byte IP header) ---
    out.insert(out.end(), data.begin() + 20, data.end());

    // --- Update state ---
    comp_tx_id_++;

    return out;
}
// ============================================================
// Compute MAC-I — CRC32 over (integrity_key || count || data).
// Produces a 4-byte integrity check value.
// ============================================================
uint32_t PdcpLayer::compute_mac_i(uint32_t count, const std::vector<uint8_t>& data) {
    if (config_.integrity_algorithm == 0) {
        // V1: CRC32 integrity (original code, unchanged)
        std::vector<uint8_t> buf;
        buf.reserve(16 + 4 + data.size());

        buf.insert(buf.end(), config_.integrity_key, config_.integrity_key + 16);

        buf.push_back(static_cast<uint8_t>((count >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((count >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((count >> 8)  & 0xFF));
        buf.push_back(static_cast<uint8_t>(count & 0xFF));

        buf.insert(buf.end(), data.begin(), data.end());

        return crc32(buf.data(), buf.size());
    } else if (config_.integrity_algorithm == 1) {
        // Optimized: HMAC-SHA256 (per TS 38.323 §5.9)
        return compute_mac_i_hmac(count, data);
    }

    return 0;
}

// ============================================================
// HMAC-SHA256 integrity — replaces CRC32 when
// config_.integrity_algorithm == 1.
//
// Per TS 38.323 §5.9, integrity input includes COUNT, BEARER,
// DIRECTION and the message. We compute HMAC-SHA256 and truncate
// to 4 bytes to fit the MAC-I field.
// AI-assisted: reviewed by Member 1
// ============================================================
uint32_t PdcpLayer::compute_mac_i_hmac(uint32_t count, const std::vector<uint8_t>& data) {
    // Build integrity input: COUNT || BEARER || DIRECTION || payload
    std::vector<uint8_t> message;
    message.reserve(6 + data.size());

    // COUNT (4 bytes, big-endian)
    message.push_back(static_cast<uint8_t>((count >> 24) & 0xFF));
    message.push_back(static_cast<uint8_t>((count >> 16) & 0xFF));
    message.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    message.push_back(static_cast<uint8_t>(count & 0xFF));

    // BEARER (1 byte)
    message.push_back(0x01);

    // DIRECTION (1 byte) — 0 for both TX/RX in loopback
    message.push_back(0x00);

    // Append the ciphered payload
    message.insert(message.end(), data.begin(), data.end());

    // Compute HMAC-SHA256 using the integrity key
    unsigned char hmac_result[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;

    HMAC(EVP_sha256(),
         config_.integrity_key, 16,
         message.data(), message.size(),
         hmac_result, &hmac_len);

    // Truncate to 4 bytes for MAC-I field
    uint32_t mac_i = (static_cast<uint32_t>(hmac_result[0]) << 24) |
                     (static_cast<uint32_t>(hmac_result[1]) << 16) |
                     (static_cast<uint32_t>(hmac_result[2]) << 8)  |
                     (static_cast<uint32_t>(hmac_result[3]));

    return mac_i;
}

// ============================================================
// Member 2: Header Decompression (FINAL VERSION)
//
// Strategy:
//   - If marker (0xFC) not present → packet is uncompressed
//     → use it to initialize context if needed
//
//   - If compressed:
//       Restore full 20-byte header using stored context
//       Update total length field
//       Append payload
//
// Guarantees:
//   - Lossless reconstruction of original IPv4 packet
// ============================================================
std::vector<uint8_t> PdcpLayer::decompress_header(const std::vector<uint8_t>& data) {

    // ========================================================
    // Case 1: Not compressed → pass through
    // ========================================================
    if (data.size() < 1 || data[0] != 0xFC) {

        // If this is an IPv4 packet, initialize context
        if (data.size() >= 20 && (data[0] >> 4) == 4 && !context_initialized_) {
            stored_ip_header_.assign(data.begin(), data.begin() + 20);
            context_initialized_ = true;
        }

        return data;
    }

    // ========================================================
    // Case 2: Compressed packet
    // ========================================================
    if (!context_initialized_) {
        // Cannot decompress without context
        return {}; // fail safely
    }

    size_t offset = 1;

    // --- Packet ID (not strictly needed for reconstruction) ---
    uint16_t pkt_id = (data[offset] << 8) | data[offset + 1];
    offset += 2;

    // --- Total length ---
    uint16_t total_len = (data[offset] << 8) | data[offset + 1];
    offset += 2;

    std::vector<uint8_t> out;

    // ========================================================
    // Restore full IPv4 header from stored context
    // ========================================================
    out.insert(out.end(), stored_ip_header_.begin(), stored_ip_header_.end());

    // --- Update total length field (bytes 2–3) ---
    out[2] = (total_len >> 8) & 0xFF;
    out[3] = total_len & 0xFF;

    // ========================================================
    // Append payload
    // ========================================================
    out.insert(out.end(), data.begin() + offset, data.end());

    // --- Update RX state ---
    comp_rx_id_++;

    return out;
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
    // ============================================================
    // TX path modification (Member 2)
    //
    // Compression is applied AFTER receiving SDU
    // and BEFORE ciphering, as required by PDCP spec.
    // ============================================================
    // --- Step 3:compression
    if (config_.compression_enabled) {
        payload = compress_header(payload);
    }
    // --- Step 4: Cipher the payload ---
    if (config_.ciphering_enabled) {
        apply_cipher(payload, count);
    }

    // --- Step 5: Compute MAC-I over the ciphered payload ---
    uint32_t mac_i = 0;
    if (config_.integrity_enabled) {
        mac_i = compute_mac_i(count, payload);
    }

    // --- Step 6: Assemble the PDCP PDU ---
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

    // ============================================================
    // RX path modification (Member 2)
    //
    // Decompression is applied AFTER deciphering
    // and BEFORE delivering SDU to upper layer.
    // ============================================================
    // --- Step 6: Decompression
    if (config_.compression_enabled) {
        payload = decompress_header(payload);
    }
    // --- Step 7: Return the recovered SDU ---
    ByteBuffer sdu;
    sdu.data = std::move(payload);
    return sdu;
}
