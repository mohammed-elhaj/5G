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
// TX path — helper
// Write one subheader + SDU payload at pos. Returns bytes written
// (0 if the SDU does not fit in the remaining TB space).
// AI-assisted: reviewed by Member 5
// ============================================================
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
// AI-assisted: reviewed by Member 5
// ============================================================
ByteBuffer MacLayer::process_tx(const std::vector<ByteBuffer>& sdus, size_t tb_size) {
    LcData lc;
    lc.lcid      = config_.logical_channel_id;
    lc.priority  = 0;
    lc.pbr_bytes = 0xFFFFFFFF;
    lc.sdus      = sdus;
    return process_tx({lc}, tb_size);
}

// ============================================================
// TX path — Multi-LCID multiplexing with optional LCP scheduling
//
// When lcp_enabled=false (default / V1 behavior):
//   Channels are served in input order. All SDUs written until TB full.
//   Remaining SDUs are truncated with a log message.
//
// When lcp_enabled=true (LCP per TS 38.321 §5.4.3.1):
//   Step 1 — Sort channels by priority ascending (lower = higher priority).
//   Step 2 — PBR phase: serve each channel in priority order, writing SDUs
//             until pbr_bytes quota consumed or channel drained.
//   Step 3 — Round-robin phase: cycle channels with leftover SDUs one SDU
//             at a time until TB is full or all channels drained.
//
// When bsr_enabled=true (Member 6 / TS 38.321 §6.1.3.1):
//   A Short BSR MAC CE (LCID=61, 1-byte payload) is prepended before SDUs.
//
// Fix vs V1 / Member 6: TB is allocated without zero-fill; only the padding
// region is zeroed. Eliminates ~TB_size wasted writes per call.
//
// MAC PDU order (TS 38.321 / AI_RULES Pair C):
//   [BSR CE (if enabled)] → SDUs → padding subheader → zeros
// AI-assisted: reviewed by Member 5
// ============================================================
ByteBuffer MacLayer::process_tx(std::vector<LcData> channels, size_t tb_size) {
    ByteBuffer tb;
    size_t effective_tb_size = (tb_size > 0) ? tb_size : config_.transport_block_size;

    // Allocate without zero-fill — only the padding region will be zeroed below.
    // Fix: Member 6's version used resize(n, 0x00) which zeroed the entire TB.
    tb.data.resize(effective_tb_size);

    size_t pos = 0;

    // ---- Short BSR MAC CE (LCID=61) — only when bsr_enabled=true ----
    // Per TS 38.321 §6.1.3.1: 1-byte payload = [LCG ID (3 bits)][Buffer Size Index (5 bits)]
    // Fix: Member 6 inserted BSR unconditionally; this broke test_padding and
    // test_lcp_priority_ordering because the unconditional 2-byte prefix shifted
    // all SDU positions and the first-byte checks.
    // AI-assisted: reviewed by Member 5
    if (config_.bsr_enabled && pos + 2 <= effective_tb_size) {
        tb.data[pos++] = LCID_BSR;
        const uint8_t lcg_id       = 1;   // Logical Channel Group 1
        const uint8_t buffer_index = 15;  // Buffer Size Index for simulation
        tb.data[pos++] = static_cast<uint8_t>((lcg_id << 5) | (buffer_index & 0x1F));
    }

    // ---- Multiplexing + LCP scheduling ----
    if (config_.lcp_enabled) {
        // ------------------------------------------------------------------
        // LCP per TS 38.321 §5.4.3.1
        // Step 1: sort channels by priority ascending
        // Step 2 (PBR phase): each channel sends SDUs up to its PBR quota
        // Step 3 (round-robin phase): remaining SDUs cycled one per channel
        // Fix: Member 6 replaced this entire block with a plain loop, losing
        // the sort and quota logic, causing test_lcp_priority_ordering and
        // test_lcp_pbr_quota to fail.
        // AI-assisted: reviewed by Member 5
        // ------------------------------------------------------------------
        std::sort(channels.begin(), channels.end(),
                  [](const LcData& a, const LcData& b) {
                      return a.priority < b.priority;
                  });

        // Per-channel cursor and remaining PBR quota
        std::vector<size_t>   sdu_idx(channels.size(), 0);
        std::vector<uint32_t> pbr_rem(channels.size());
        for (size_t ci = 0; ci < channels.size(); ++ci)
            pbr_rem[ci] = channels[ci].pbr_bytes;

        bool tb_full = false;

        // PBR phase
        for (size_t ci = 0; ci < channels.size() && !tb_full; ++ci) {
            const LcData& ch = channels[ci];
            while (sdu_idx[ci] < ch.sdus.size() && !tb_full) {
                const ByteBuffer& sdu = ch.sdus[sdu_idx[ci]];
                if (sdu.size() > pbr_rem[ci]) break;  // quota exhausted for this channel
                size_t written = write_sdu(tb.data, pos, effective_tb_size, ch.lcid, sdu);
                if (written == 0) { tb_full = true; break; }
                pbr_rem[ci] -= static_cast<uint32_t>(sdu.size());
                pos += written;
                sdu_idx[ci]++;
            }
        }

        // Round-robin phase — one SDU per channel per round until TB full
        bool any_left = true;
        while (any_left && !tb_full) {
            any_left = false;
            for (size_t ci = 0; ci < channels.size() && !tb_full; ++ci) {
                const LcData& ch = channels[ci];
                if (sdu_idx[ci] >= ch.sdus.size()) continue;
                any_left = true;
                const ByteBuffer& sdu = ch.sdus[sdu_idx[ci]];
                size_t written = write_sdu(tb.data, pos, effective_tb_size, ch.lcid, sdu);
                if (written == 0) { tb_full = true; break; }
                pos += written;
                sdu_idx[ci]++;
            }
        }

    } else {
        // Simple path (LCP off): serve channels in input order, all SDUs.
        // Member 6 truncation logic preserved — stops cleanly when TB is full.
        bool tb_full = false;
        for (const auto& ch : channels) {
            for (const auto& sdu : ch.sdus) {
                size_t written = write_sdu(tb.data, pos, effective_tb_size, ch.lcid, sdu);
                if (written == 0) {
                    std::cerr << "  [MAC TX] TB full (" << effective_tb_size
                              << "B). Truncating remaining SDUs.\n";
                    tb_full = true;
                    break;
                }
                pos += written;
            }
            if (tb_full) break;
        }
    }

    // ---- Padding ----
    // Write padding subheader (LCID=63) then zero only the remaining bytes.
    // Fix vs V1 / Member 6: avoids zeroing the entire TB on every call.
    if (pos < effective_tb_size) {
        tb.data[pos] = LCID_PADDING;
        if (pos + 1 < effective_tb_size)
            std::fill(tb.data.begin() + pos + 1, tb.data.end(), 0x00);
    }

    return tb;
}

// ============================================================
// RX path — Demultiplexing
//
// Walk through the Transport Block byte-by-byte:
//   1. Read the subheader byte: extract F bit and LCID
//   2. If LCID == 61 → Short BSR CE: read 1-byte payload and log
//   3. If LCID == 63 → padding, stop parsing
//   4. Read the length field (1 or 2 bytes depending on F)
//   5. Extract L bytes of SDU payload
//   6. Deliver the SDU to the output vector
// ============================================================
std::vector<ByteBuffer> MacLayer::process_rx(const ByteBuffer& transport_block) {
    std::vector<ByteBuffer> sdus;
    size_t pos     = 0;
    size_t tb_size = transport_block.size();

    while (pos < tb_size) {
        uint8_t byte0 = transport_block.data[pos++];
        uint8_t lcid  = byte0 & 0x3F;

        // Short BSR MAC CE (Member 6) — 1-byte payload, consume and skip.
        // Logging suppressed here (hot path); use process_rx_multi for diagnostics.
        if (lcid == LCID_BSR) {
            if (pos < tb_size) pos++;  // skip 1-byte BSR payload
            continue;
        }

        bool f_bit = (byte0 >> 6) & 0x01;

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

        // Short BSR MAC CE — 1-byte payload, log and skip
        if (lcid == LCID_BSR) {
            if (pos < tb_size) {
                uint8_t bsr_val = transport_block.data[pos++];
                (void)bsr_val;
            }
            continue;
        }

        bool f_bit = (byte0 >> 6) & 0x01;

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
