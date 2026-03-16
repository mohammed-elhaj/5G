#pragma once
// ============================================================
// mac.h — MAC Layer (TS 38.321 simplified)
//
// TX (multiplexing):
//   Receives MAC SDUs (= RLC PDUs) and packs them into a single
//   Transport Block with per-SDU subheaders and padding.
//
// RX (demultiplexing):
//   Parses the Transport Block, extracts MAC SDUs by subheader.
//
// NR MAC subheader format (placed immediately before each payload):
//   Byte 0: [R(1 bit)=0][F(1 bit)][LCID(6 bits)]
//   If F=0: Byte 1 = L (8-bit length,  SDU ≤ 255 bytes)
//   If F=1: Byte 1-2 = L (16-bit length, SDU > 255 bytes)
//
// Padding subheader: LCID = 63, no length field, rest of TB is padding.
// ============================================================

#include "common.h"
#include <vector>

class MacLayer {
public:
    explicit MacLayer(const Config& cfg);

    /// Uplink TX: pack multiple SDUs into one Transport Block (V1 — single LCID).
    ByteBuffer process_tx(const std::vector<ByteBuffer>& sdus);

    /// Uplink TX: multi-LCID — pack SDUs from multiple logical channels.
    /// Channels are served in LcData.priority order (lower value = higher priority).
    ByteBuffer process_tx(std::vector<LcData> channels);

    /// Downlink RX: extract SDUs from one Transport Block (V1 — discards LCID).
    std::vector<ByteBuffer> process_rx(const ByteBuffer& transport_block);

    /// Downlink RX: multi-LCID — returns SDUs tagged with their LCID.
    std::vector<std::pair<uint8_t, ByteBuffer>> process_rx_multi(const ByteBuffer& transport_block);

    void reset();

private:
    Config config_;

    static constexpr uint8_t LCID_PADDING = 63;  // Padding LCID per TS 38.321
};
