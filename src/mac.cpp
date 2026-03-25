#include "mac.h"
#include <algorithm>
#include <cstring>
#include <iostream>

MacLayer::MacLayer(const Config& cfg) : config_(cfg) {}

void MacLayer::reset() {
}

// Write one MAC subPDU (R/F/LCID header + length + SDU payload) into the transport block.
// Returns total bytes written (header + payload), or 0 if insufficient space (TS 38.321 §6.1.2).
static size_t write_sdu(std::vector<uint8_t>& tb_data, size_t pos, size_t tb_size,
                        uint8_t lcid, const ByteBuffer& sdu) {
    uint32_t sdu_len     = static_cast<uint32_t>(sdu.size());
    bool     long_format = (sdu_len > 255);
    size_t   hdr_size    = long_format ? 3 : 2;

    if (pos + hdr_size + sdu_len > tb_size) return 0;

    // R/F/LCID byte: F=1 for 16-bit length, F=0 for 8-bit length (TS 38.321 §6.1.2)
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

// Single-channel convenience wrapper: wrap SDUs into one LcData and delegate to multi-channel process_tx.
ByteBuffer MacLayer::process_tx(const std::vector<ByteBuffer>& sdus, size_t tb_size) {
    LcData lc;
    lc.lcid      = config_.logical_channel_id;
    lc.priority  = 0;
    lc.pbr_bytes = 0xFFFFFFFF;
    lc.sdus      = sdus;
    return process_tx({lc}, tb_size);
}

ByteBuffer MacLayer::process_tx(std::vector<LcData> channels, size_t tb_size) {
    ByteBuffer tb;
    size_t effective_tb_size = (tb_size > 0) ? tb_size : config_.transport_block_size;

    tb.data.resize(effective_tb_size);

    size_t pos = 0;

    // Insert Buffer Status Report MAC CE if enabled (TS 38.321 §6.1.3.1)
    if (config_.bsr_enabled && pos + 2 <= effective_tb_size) {
        tb.data[pos++] = LCID_BSR;
        const uint8_t lcg_id       = 1;
        const uint8_t buffer_index = 15;
        tb.data[pos++] = static_cast<uint8_t>((lcg_id << 5) | (buffer_index & 0x1F));
    }

    // Logical Channel Prioritization (LCP) scheduling per TS 38.321 §5.4.3.1
    if (config_.lcp_enabled) {
        // Sort channels by priority ascending (lower value = higher priority)
        std::sort(channels.begin(), channels.end(),
                  [](const LcData& a, const LcData& b) {
                      return a.priority < b.priority;
                  });

        const size_t num_ch = channels.size();
        std::vector<size_t>   sdu_idx(num_ch, 0);
        std::vector<uint32_t> pbr_rem;
        pbr_rem.reserve(num_ch);
        for (const auto& ch : channels)
            pbr_rem.push_back(ch.pbr_bytes);

        bool tb_full = false;

        // Phase 1: serve each channel up to its PBR quota in priority order
        for (size_t ci = 0; ci < num_ch && !tb_full; ++ci) {
            const LcData& ch = channels[ci];
            while (sdu_idx[ci] < ch.sdus.size() && !tb_full) {
                const ByteBuffer& sdu = ch.sdus[sdu_idx[ci]];
                if (sdu.size() > pbr_rem[ci]) break;
                size_t written = write_sdu(tb.data, pos, effective_tb_size, ch.lcid, sdu);
                if (written == 0) { tb_full = true; break; }
                pbr_rem[ci] -= static_cast<uint32_t>(sdu.size());
                pos += written;
                sdu_idx[ci]++;
            }
        }

        // Phase 2: round-robin across all channels for remaining SDUs
        bool any_left = true;
        while (any_left && !tb_full) {
            any_left = false;
            for (size_t ci = 0; ci < num_ch && !tb_full; ++ci) {
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
        // No LCP: pack SDUs in channel order until TB is full
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

    // Fill remaining space with padding subheader (TS 38.321 §6.1.2)
    if (pos < effective_tb_size) {
        tb.data[pos] = LCID_PADDING;
        if (pos + 1 < effective_tb_size)
            std::fill(tb.data.begin() + pos + 1, tb.data.end(), 0x00);
    }

    return tb;
}

// Demultiplex transport block into SDUs, discarding LCID information (TS 38.321 §6.1.2).
std::vector<ByteBuffer> MacLayer::process_rx(const ByteBuffer& transport_block) {
    size_t tb_size = transport_block.size();
    std::vector<ByteBuffer> sdus;
    sdus.reserve(tb_size / 3);

    size_t pos = 0;
    while (pos < tb_size) {
        uint8_t byte0 = transport_block.data[pos++];
        uint8_t lcid  = byte0 & 0x3F;

        if (lcid == LCID_PADDING) break;

        // Skip MAC CEs (e.g., BSR)
        if (lcid == LCID_BSR) {
            if (pos < tb_size) pos++;
            continue;
        }

        // Parse length field: F=1 → 16-bit, F=0 → 8-bit (TS 38.321 §6.1.2)
        bool f_bit = (byte0 >> 6) & 0x01;

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

// Demultiplex transport block into (LCID, SDU) pairs for multi-channel reception (TS 38.321 §6.1.2).
std::vector<std::pair<uint8_t, ByteBuffer>>
MacLayer::process_rx_multi(const ByteBuffer& transport_block) {
    size_t tb_size = transport_block.size();
    std::vector<std::pair<uint8_t, ByteBuffer>> result;
    result.reserve(tb_size / 3);

    size_t pos = 0;
    while (pos < tb_size) {
        uint8_t byte0 = transport_block.data[pos++];
        uint8_t lcid  = byte0 & 0x3F;

        if (lcid == LCID_PADDING) break;

        // Skip MAC CEs (e.g., BSR)
        if (lcid == LCID_BSR) {
            if (pos < tb_size) {
                uint8_t bsr_val = transport_block.data[pos++];
                (void)bsr_val;
            }
            continue;
        }

        // Parse length field: F=1 → 16-bit, F=0 → 8-bit (TS 38.321 §6.1.2)
        bool f_bit = (byte0 >> 6) & 0x01;

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
