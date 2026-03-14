#pragma once
// ============================================================
// rlc.h — RLC Layer, UM mode (TS 38.322 simplified)
//
// TX (segmentation):
//   RLC SDU → if fits in max_pdu_size, send as complete SDU (SI=00)
//           → otherwise, segment into first/middle/last PDUs
//
// RX (reassembly):
//   Collect segments by SN, reassemble when all pieces arrive.
//
// Key NR rule: one RLC PDU = one SDU or one SDU segment (no
// concatenation across SDU boundaries).
// ============================================================

#include "common.h"
#include <vector>
#include <unordered_map>

class RlcLayer {
public:
    explicit RlcLayer(const Config& cfg);

    /// Uplink TX: segment one SDU into one or more RLC PDUs.
    std::vector<ByteBuffer> process_tx(const ByteBuffer& sdu);

    /// Downlink RX: feed one RLC PDU.  Returns a vector of any
    /// fully-reassembled SDUs (usually 0 or 1).
    std::vector<ByteBuffer> process_rx(const ByteBuffer& pdu);

    void reset();

private:
    uint8_t  tx_next_sn_ = 0;   // Next SN to assign on TX side
    Config   config_;
    uint8_t  sn_modulus_;        // 2^sn_length (64 for 6-bit, 4096 for 12-bit)

    // ---- Reassembly state (RX side) ----
    struct RxSegment {
        uint16_t              offset;   // Byte offset within the original SDU
        std::vector<uint8_t>  data;
        uint8_t               si;       // Segment indicator that produced this piece
    };
    // Map from SN → collected segments (unordered; sorted at reassembly time)
    std::unordered_map<uint8_t, std::vector<RxSegment>> rx_buffer_;

    /// Try to reassemble a complete SDU for the given SN.
    /// Returns an empty ByteBuffer if not all segments have arrived yet.
    ByteBuffer try_reassemble(uint8_t sn);
};
