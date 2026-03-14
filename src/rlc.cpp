// ============================================================
// rlc.cpp — RLC Layer implementation, UM mode (TS 38.322)
//
// RLC UM PDU formats (6-bit SN):
//
//   Complete SDU (SI=00):
//     Byte 0: [SI(2)=00][SN(6)]
//     Byte 1..N: Data
//
//   First segment (SI=01):
//     Byte 0: [SI(2)=01][SN(6)]
//     Byte 1..N: Data
//
//   Middle segment (SI=11):
//     Byte 0: [SI(2)=11][SN(6)]
//     Byte 1-2: SO (16-bit segment offset, big-endian)
//     Byte 3..N: Data
//
//   Last segment (SI=10):
//     Byte 0: [SI(2)=10][SN(6)]
//     Byte 1-2: SO (16-bit segment offset, big-endian)
//     Byte 3..N: Data
// ============================================================

#include "rlc.h"
#include <algorithm>
#include <cstring>
#include <iostream>

RlcLayer::RlcLayer(const Config& cfg)
    : config_(cfg)
    , sn_modulus_(static_cast<uint8_t>(1 << cfg.rlc_sn_length))   // 64 for 6-bit
{
}

void RlcLayer::reset() {
    tx_next_sn_ = 0;
    rx_buffer_.clear();
}

// ============================================================
// TX path — Segmentation
//
// If the SDU (PDCP PDU) fits within rlc_max_pdu_size including
// the 1-byte header, emit a single complete-SDU PDU (SI=00).
//
// Otherwise, chop the SDU into segments that each fit within
// rlc_max_pdu_size.  The first segment gets SI=01 and a 1-byte
// header.  Middle and last segments get SI=11/10 with a 3-byte
// header (1 byte SI+SN + 2 bytes SO).
// ============================================================
std::vector<ByteBuffer> RlcLayer::process_tx(const ByteBuffer& sdu) {
    std::vector<ByteBuffer> pdus;
    uint8_t sn = tx_next_sn_;
    tx_next_sn_ = (tx_next_sn_ + 1) % sn_modulus_;

    uint32_t max_pdu = config_.rlc_max_pdu_size;

    // Header sizes: complete/first = 1 byte, middle/last = 3 bytes (1 + 2 SO)
    const size_t header_complete = 1;
    const size_t header_with_so  = 3;

    // Check if the SDU fits in a single PDU (header + data ≤ max_pdu)
    if (sdu.size() + header_complete <= max_pdu) {
        // --- Complete SDU (SI=00) ---
        ByteBuffer pdu;
        pdu.data.resize(header_complete + sdu.size());
        // Byte 0: [SI=00 (2 bits)][SN (6 bits)]
        pdu.data[0] = (0x00 << 6) | (sn & 0x3F);
        std::memcpy(pdu.data.data() + header_complete, sdu.data.data(), sdu.size());
        pdus.push_back(std::move(pdu));
        return pdus;
    }

    // --- Segmentation required ---
    size_t offset = 0;
    size_t remaining = sdu.size();
    bool first = true;

    while (remaining > 0) {
        size_t header_size;
        uint8_t si;
        size_t max_data;
        size_t data_len;

        if (first) {
            // First segment: SI=01, 1-byte header (no SO field)
            header_size = header_complete;
            max_data = max_pdu - header_size;
            data_len = std::min(remaining, max_data);
            si = 0x01;  // SI bits = 01
            first = false;
        } else {
            // Middle or last segment: 3-byte header (with SO)
            header_size = header_with_so;
            max_data = max_pdu - header_size;
            data_len = std::min(remaining, max_data);

            if (remaining <= max_data) {
                si = 0x02;  // SI bits = 10 → last segment
            } else {
                si = 0x03;  // SI bits = 11 → middle segment
            }
        }

        // Build the PDU
        ByteBuffer pdu;
        pdu.data.resize(header_size + data_len);

        // Byte 0: [SI (2 bits)][SN (6 bits)]
        pdu.data[0] = static_cast<uint8_t>((si << 6) | (sn & 0x3F));

        // For middle/last segments, write the 16-bit Segment Offset
        if (header_size == header_with_so) {
            pdu.data[1] = static_cast<uint8_t>((offset >> 8) & 0xFF);
            pdu.data[2] = static_cast<uint8_t>(offset & 0xFF);
        }

        // Copy the segment data
        std::memcpy(pdu.data.data() + header_size,
                    sdu.data.data() + offset,
                    data_len);

        pdus.push_back(std::move(pdu));

        offset    += data_len;
        remaining -= data_len;
    }

    return pdus;
}

// ============================================================
// RX path — Reassembly
//
// Each received PDU is parsed for SI, SN, and (if present) SO.
// Segments are buffered by SN.  After each PDU we attempt to
// reassemble: if we have a complete SDU (SI=00) or the full
// first-through-last chain, we return the reassembled SDU.
// ============================================================
std::vector<ByteBuffer> RlcLayer::process_rx(const ByteBuffer& pdu) {
    std::vector<ByteBuffer> result;

    if (pdu.empty()) return result;

    // --- Parse the RLC header ---
    uint8_t si = (pdu.data[0] >> 6) & 0x03;
    uint8_t sn = pdu.data[0] & 0x3F;

    RxSegment seg;
    seg.si = si;

    if (si == 0x00) {
        // Complete SDU — no segmentation, data starts at byte 1
        seg.offset = 0;
        seg.data.assign(pdu.data.begin() + 1, pdu.data.end());
    } else if (si == 0x01) {
        // First segment — data starts at byte 1, offset is 0
        seg.offset = 0;
        seg.data.assign(pdu.data.begin() + 1, pdu.data.end());
    } else {
        // Middle (SI=11=0x03) or Last (SI=10=0x02) — has 2-byte SO after byte 0
        seg.offset = (static_cast<uint16_t>(pdu.data[1]) << 8) | pdu.data[2];
        seg.data.assign(pdu.data.begin() + 3, pdu.data.end());
    }

    // Store the segment
    rx_buffer_[sn].push_back(std::move(seg));

    // If it was a complete SDU, we can return it immediately
    if (si == 0x00) {
        ByteBuffer sdu;
        sdu.data = std::move(rx_buffer_[sn][0].data);
        rx_buffer_.erase(sn);
        result.push_back(std::move(sdu));
        return result;
    }

    // Try to reassemble from collected segments
    ByteBuffer assembled = try_reassemble(sn);
    if (!assembled.empty()) {
        rx_buffer_.erase(sn);
        result.push_back(std::move(assembled));
    }

    return result;
}

// ============================================================
// try_reassemble — check whether all segments for this SN have
// arrived and, if so, stitch them together in offset order.
//
// We need at least one first-segment (SI=01) and one last-segment
// (SI=10), with middle segments (SI=11) filling any gaps.  The
// total assembled size must equal (last_segment_offset + last_segment_length).
// ============================================================
ByteBuffer RlcLayer::try_reassemble(uint8_t sn) {
    auto it = rx_buffer_.find(sn);
    if (it == rx_buffer_.end()) return ByteBuffer{};

    auto& segments = it->second;

    // Check we have both a first and a last segment
    bool has_first = false, has_last = false;
    for (auto& s : segments) {
        if (s.si == 0x01) has_first = true;
        if (s.si == 0x02) has_last = true;
    }
    if (!has_first || !has_last) return ByteBuffer{};

    // Sort segments by offset
    std::sort(segments.begin(), segments.end(),
              [](const RxSegment& a, const RxSegment& b) {
                  return a.offset < b.offset;
              });

    // Determine total SDU size from the last segment
    size_t total_size = 0;
    for (auto& s : segments) {
        size_t end = s.offset + s.data.size();
        if (end > total_size) total_size = end;
    }

    // Verify contiguous coverage: each segment must start exactly
    // where the previous one ended.
    size_t expected_offset = 0;
    for (auto& s : segments) {
        if (s.offset != expected_offset) {
            // Gap detected — not all segments have arrived yet
            return ByteBuffer{};
        }
        expected_offset = s.offset + s.data.size();
    }

    // All segments present and contiguous — assemble
    ByteBuffer sdu;
    sdu.data.resize(total_size);
    for (auto& s : segments) {
        std::memcpy(sdu.data.data() + s.offset, s.data.data(), s.data.size());
    }

    return sdu;
}
