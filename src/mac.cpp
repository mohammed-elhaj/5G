// ============================================================
// mac.cpp — MAC Layer implementation (TS 38.321 simplified)
//
// NR MAC PDU structure (subheaders inline with payloads):
//   [subheader_1][payload_1][subheader_2][payload_2]...[padding_subheader][padding_bytes]
//
// Unlike LTE (where all subheaders are grouped at the front),
// NR places each subheader immediately before its payload.
// This simplifies both packing and parsing.
// ============================================================

#include "mac.h"
#include <cstring>
#include <iostream>

MacLayer::MacLayer(const Config& cfg) : config_(cfg) {}

void MacLayer::reset() {
    // MAC is stateless in V1 (no HARQ buffers, no BSR state)
}

// ============================================================
// TX path — Multiplexing
//
// For each MAC SDU (RLC PDU):
//   1. Determine F bit: F=0 if SDU ≤ 255 bytes, F=1 otherwise
//   2. Write the subheader: [R=0][F][LCID(6)]  then L (1 or 2 bytes)
//   3. Copy the SDU payload
//
// After all SDUs, if there is remaining space in the Transport
// Block, write a padding subheader (LCID=63) and fill with zeros.
// ============================================================
ByteBuffer MacLayer::process_tx(const std::vector<ByteBuffer>& sdus) {
    ByteBuffer tb;
    tb.data.resize(config_.transport_block_size, 0x00);  // pre-fill with zeros

    size_t pos = 0;

    for (const auto& sdu : sdus) {
        uint32_t sdu_len = static_cast<uint32_t>(sdu.size());
        bool long_format = (sdu_len > 255);   // F=1 for long SDUs

        // Subheader size: 1 byte (R/F/LCID) + 1 or 2 bytes (L field)
        size_t subheader_size = long_format ? 3 : 2;
        size_t total_needed   = subheader_size + sdu_len;

        // Safety check: do we have room?
        if (pos + total_needed > config_.transport_block_size) {
            std::cerr << "  [MAC TX] WARNING: Transport Block overflow, skipping SDU of "
                      << sdu_len << " bytes\n";
            continue;
        }

        // --- Write the subheader ---
        // Byte 0: [R=0(1 bit)][F(1 bit)][LCID(6 bits)]
        uint8_t byte0 = config_.logical_channel_id & 0x3F;
        if (long_format) {
            byte0 |= 0x40;   // Set the F bit (bit 6)
        }
        tb.data[pos++] = byte0;

        // Length field (big-endian for 16-bit)
        if (long_format) {
            tb.data[pos++] = static_cast<uint8_t>((sdu_len >> 8) & 0xFF);
            tb.data[pos++] = static_cast<uint8_t>(sdu_len & 0xFF);
        } else {
            tb.data[pos++] = static_cast<uint8_t>(sdu_len & 0xFF);
        }

        // --- Copy the SDU payload ---
        std::memcpy(tb.data.data() + pos, sdu.data.data(), sdu_len);
        pos += sdu_len;
    }

    // --- Add padding subheader if there is remaining space ---
    if (pos < config_.transport_block_size) {
        // Padding subheader: [R=0][F=0][LCID=63]
        tb.data[pos++] = LCID_PADDING;
        // Remaining bytes are already zero (padding)
    }

    return tb;
}

// ============================================================
// RX path — Demultiplexing
//
// Walk through the Transport Block byte-by-byte:
//   1. Read the subheader byte: extract F bit and LCID
//   2. If LCID == 63 → padding, stop parsing
//   3. Read the length field (1 or 2 bytes depending on F)
//   4. Extract L bytes of SDU payload
//   5. Deliver the SDU to the output vector
// ============================================================
std::vector<ByteBuffer> MacLayer::process_rx(const ByteBuffer& transport_block) {
    std::vector<ByteBuffer> sdus;
    size_t pos = 0;
    size_t tb_size = transport_block.size();

    while (pos < tb_size) {
        // --- Read the subheader byte ---
        uint8_t byte0 = transport_block.data[pos++];
        uint8_t lcid  = byte0 & 0x3F;
        bool    f_bit = (byte0 >> 6) & 0x01;

        // Check for padding LCID — stop parsing
        if (lcid == LCID_PADDING) {
            break;
        }

        // --- Read the length field ---
        uint32_t sdu_len = 0;
        if (f_bit) {
            // 16-bit length (big-endian)
            if (pos + 2 > tb_size) break;
            sdu_len = (static_cast<uint32_t>(transport_block.data[pos]) << 8) |
                       static_cast<uint32_t>(transport_block.data[pos + 1]);
            pos += 2;
        } else {
            // 8-bit length
            if (pos + 1 > tb_size) break;
            sdu_len = transport_block.data[pos++];
        }

        // --- Extract the SDU payload ---
        if (pos + sdu_len > tb_size) {
            std::cerr << "  [MAC RX] WARNING: SDU extends beyond TB boundary\n";
            break;
        }

        ByteBuffer sdu;
        sdu.data.assign(transport_block.data.begin() + pos,
                        transport_block.data.begin() + pos + sdu_len);
        pos += sdu_len;

        sdus.push_back(std::move(sdu));
    }

    return sdus;
}
