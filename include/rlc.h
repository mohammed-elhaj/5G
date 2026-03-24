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
#include <map>
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
    Config   config_;
    uint8_t  sn_modulus_;        // 2^sn_length (64 for 6-bit UM)

    // ---- UM TX state ----
    uint8_t  tx_next_sn_ = 0;   // Next SN to assign on UM TX side

    // ---- UM RX reassembly state ----
    struct RxSegment {
        uint16_t              offset;
        std::vector<uint8_t>  data;
        uint8_t               si;
    };
    std::unordered_map<uint8_t, std::vector<RxSegment>> rx_buffer_;

    ByteBuffer try_reassemble(uint8_t sn);

    // ---- AM TX state (Member 3) ----
    uint16_t am_tx_next_sn_  = 0;   // 12-bit SN counter, mod 4096
    uint16_t poll_sn_        = 0;   // SN at last poll trigger
    uint16_t poll_pdu_count_ = 0;   // PDUs sent since last poll
    std::map<uint16_t, ByteBuffer> retx_buf_;  // retransmission buffer: SN → PDU

    // ---- AM RX state (Member 4) ----
    uint16_t rx_next_           = 0;   // lowest SN not yet delivered
    uint16_t rx_highest_status_ = 0;   // highest SN seen (for NACK range)
    struct AmRxSegment {
        uint16_t              offset;
        std::vector<uint8_t>  data;
        uint8_t               si;
    };
    std::unordered_map<uint16_t, std::vector<AmRxSegment>> am_rx_buffer_;
    std::vector<ByteBuffer> pending_status_pdus_;  // STATUS PDUs queued for return

    // ---- STATUS PDU result struct ----
    struct StatusPdu {
        uint16_t              ack_sn;
        std::vector<uint16_t> nack_sns;
        bool                  valid = false;
    };

    // ---- UM helpers (extracted from V1 for clean dispatch) ----
    std::vector<ByteBuffer> process_tx_um(const ByteBuffer& sdu);
    std::vector<ByteBuffer> process_rx_um(const ByteBuffer& pdu);

    // ---- AM TX helpers (Member 3) ----
    std::vector<ByteBuffer> process_tx_am(const ByteBuffer& sdu);
    ByteBuffer build_status_pdu(uint16_t ack_sn,
                                const std::vector<uint16_t>& nack_sns);

    // ---- AM RX helpers (Member 4) ----
    std::vector<ByteBuffer> process_rx_am(const ByteBuffer& pdu);
    ByteBuffer try_reassemble_am(uint16_t sn);

    // ---- Shared STATUS PDU parser ----
    StatusPdu parse_status_pdu(const ByteBuffer& pdu);
};
