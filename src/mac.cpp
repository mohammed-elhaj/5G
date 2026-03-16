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
#include <algorithm>
#include <cstring>
#include <iostream>

MacLayer::MacLayer(const Config& cfg) : config_(cfg) {}

void MacLayer::reset() {
    // MAC is stateless in V1 (no HARQ buffers, no BSR state)
}

// ============================================================
// TX path — helpers
// ============================================================

// Write one subheader + SDU payload at pos. Returns bytes written.
// AI-assisted: reviewed by Member 5
static size_t write_sdu(std::vector<uint8_t>& tb_data, size_t pos, size_t tb_size,
                        uint8_t lcid, const ByteBuffer& sdu) {
    uint32_t sdu_len     = static_cast<uint32_t>(sdu.size());
    bool     long_format = (sdu_len > 255);
    size_t   hdr_size    = long_format ? 3 : 2;

    if (pos + hdr_size + sdu_len > tb_size) return 0;  // no room

    // Byte 0: [R=0][F][LCID(6)]
    uint8_t byte0 = lcid & 0x3F;
    if (long_format) byte0 |= 0x40;
    tb_data[pos++] = byte0;

    if (long_format) {
        tb_data[pos++] = static_cast<uint8_t>((sdu_len >> 8) & 0xFF);
        tb_data[pos++] = static_cast<uint8_t>(sdu_len & 0xFF);
    } else {
        tb_data[pos++] = static_cast<uint8_t>(sdu_len & 0xFF);
    }

    std::memcpy(tb_data.data() + pos, sdu.data.data(), sdu_len);
    return hdr_size + sdu_len;
}

// ============================================================
// TX path — V1 backward-compatible wrapper (single LCID)
// Delegates to multi-LCID process_tx with a single LcData entry.
// All 23 original tests call this path and are unaffected.
// ============================================================
ByteBuffer MacLayer::process_tx(const std::vector<ByteBuffer>& sdus) {
    LcData lc;
    lc.lcid      = config_.logical_channel_id;
    lc.priority  = 0;
    lc.pbr_bytes = 0xFFFFFFFF;
    lc.sdus      = sdus;
    return process_tx({lc});
}

// ============================================================
// TX path — Multi-LCID multiplexing
//
// Channels are served in priority order (lower value = higher priority).
// Without LCP (lcp_enabled=false): round-robin in priority order, no quota.
// With LCP (lcp_enabled=true): PBR phase then round-robin (Phase 2 adds this).
//
// Fix vs V1: TB is allocated without zero-fill; only the padding region
// is zeroed. This eliminates ~2KB of wasted writes per call.
//
// MAC PDU order (TS 38.321 / AI_RULES Pair C):
//   SDUs → padding subheader → zeros
// ============================================================
ByteBuffer MacLayer::process_tx(std::vector<LcData> channels) {
    // Sort channels by priority (lower number = higher priority per 3GPP LCP)
    std::sort(channels.begin(), channels.end(),
              [](const LcData& a, const LcData& b) {
                  return a.priority < b.priority;
              });

    ByteBuffer tb;
    tb.data.resize(config_.transport_block_size);  // allocate, NO zero-fill (fix vs V1)
    size_t pos     = 0;
    size_t tb_size = config_.transport_block_size;

    // Write SDUs from each channel in priority order (basic, no LCP quota yet)
    for (auto& ch : channels) {
        for (const auto& sdu : ch.sdus) {
            size_t written = write_sdu(tb.data, pos, tb_size, ch.lcid, sdu);
            if (written == 0) {
                std::cerr << "  [MAC TX] WARNING: TB full, skipping SDU (LCID="
                          << static_cast<int>(ch.lcid) << " size=" << sdu.size() << ")\n";
                continue;
            }
            pos += written;
        }
    }

    // Padding subheader + zero-fill only the remaining bytes (not the entire TB)
    if (pos < tb_size) {
        tb.data[pos] = LCID_PADDING;
        std::fill(tb.data.begin() + pos + 1, tb.data.end(), 0x00);
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

// ============================================================
// RX path — Multi-LCID demultiplexing
//
// Same parsing logic as process_rx, but preserves the LCID tag
// from each subheader. Returns vector of (lcid, sdu) pairs.
// Added by Pair C Member 5.
// AI-assisted: reviewed by Member 5
// ============================================================
std::vector<std::pair<uint8_t, ByteBuffer>>
MacLayer::process_rx_multi(const ByteBuffer& transport_block) {
    std::vector<std::pair<uint8_t, ByteBuffer>> result;
    size_t pos     = 0;
    size_t tb_size = transport_block.size();

    while (pos < tb_size) {
        uint8_t byte0 = transport_block.data[pos++];
        uint8_t lcid  = byte0 & 0x3F;
        bool    f_bit = (byte0 >> 6) & 0x01;

        if (lcid == LCID_PADDING) break;

        uint32_t sdu_len = 0;
        if (f_bit) {
            if (pos + 2 > tb_size) break;
            sdu_len = (static_cast<uint32_t>(transport_block.data[pos]) << 8) |
                       static_cast<uint32_t>(transport_block.data[pos + 1]);
            pos += 2;
        } else {
            if (pos + 1 > tb_size) break;
            sdu_len = transport_block.data[pos++];
        }

        if (pos + sdu_len > tb_size) {
            std::cerr << "  [MAC RX] WARNING: SDU extends beyond TB boundary\n";
            break;
        }

        ByteBuffer sdu;
        sdu.data.assign(transport_block.data.begin() + pos,
                        transport_block.data.begin() + pos + sdu_len);
        pos += sdu_len;

        result.emplace_back(lcid, std::move(sdu));
    }

    return result;
}
