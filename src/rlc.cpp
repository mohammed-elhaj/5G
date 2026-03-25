// ============================================================
// rlc.cpp — RLC Layer implementation (TS 38.322)
//
// Supports UM mode (rlc_mode=1) and AM mode (rlc_mode=2).
// Each mode has two implementation variants selected by rlc_opt_level:
//
//   rlc_opt_level=0  V1 baseline — no reserve(), copy push_back,
//                    insert-loop reassembly, always std::sort
//   rlc_opt_level=1  Optimized   — reserve(), std::move push_back,
//                    resize()+memcpy reassembly, 2-segment sort skip
//
// Public API (process_tx / process_rx / reset) is unchanged.
// All 23 V1 tests pass with default Config (rlc_opt_level=0).
// ============================================================

#include "rlc.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>

static constexpr uint8_t AM_DC_BIT     = 0x80;
static constexpr uint8_t AM_P_BIT      = 0x40;
static constexpr uint8_t AM_SI_SHIFT   = 4;
static constexpr uint8_t AM_SN_HI_MASK = 0x0F;

RlcLayer::RlcLayer(const Config& cfg)
    : config_(cfg)
    , sn_modulus_(static_cast<uint8_t>(1 << cfg.rlc_sn_length))
{}

void RlcLayer::reset() {
    tx_next_sn_ = 0; rx_buffer_.clear();
    am_tx_next_sn_ = 0; poll_sn_ = 0; poll_pdu_count_ = 0; retx_buf_.clear();
    rx_next_ = 0; rx_highest_status_ = 0;
    am_rx_buffer_.clear(); pending_status_pdus_.clear();
}

std::vector<ByteBuffer> RlcLayer::process_tx(const ByteBuffer& sdu) {
    return (config_.rlc_mode == 2) ? process_tx_am(sdu) : process_tx_um(sdu);
}
std::vector<ByteBuffer> RlcLayer::process_rx(const ByteBuffer& pdu) {
    return (config_.rlc_mode == 2) ? process_rx_am(pdu) : process_rx_um(pdu);
}

// ============================================================
// UM TX — dispatches to baseline or optimized variant
// ============================================================
std::vector<ByteBuffer> RlcLayer::process_tx_um(const ByteBuffer& sdu) {
    std::vector<ByteBuffer> pdus;
    uint8_t sn = tx_next_sn_;
    tx_next_sn_ = (tx_next_sn_ + 1) % sn_modulus_;

    const uint32_t max_pdu      = config_.rlc_max_pdu_size;
    const size_t   hdr_complete = 1;
    const size_t   hdr_with_so  = 3;
    const bool     opt          = (config_.rlc_opt_level >= 1);

    // Single-PDU path (identical for both variants)
    if (sdu.size() + hdr_complete <= max_pdu) {
        ByteBuffer pdu;
        pdu.data.resize(hdr_complete + sdu.size());
        pdu.data[0] = sn & 0x3F;
        std::memcpy(pdu.data.data() + hdr_complete, sdu.data.data(), sdu.size());
        if (opt) pdus.push_back(std::move(pdu));
        else     pdus.push_back(pdu);
        return pdus;
    }

    // OPT: pre-allocate segment vector (V1: no reserve)
    if (opt) {
        uint32_t n = (static_cast<uint32_t>(sdu.size()) +
                      (max_pdu - hdr_with_so - 1)) / (max_pdu - hdr_with_so);
        pdus.reserve(n);
    }

    size_t offset = 0, remaining = sdu.size();
    bool first = true;
    while (remaining > 0) {
        size_t  hdr_size = first ? hdr_complete : hdr_with_so;
        size_t  max_data = max_pdu - hdr_size;
        size_t  data_len = std::min(remaining, max_data);
        uint8_t si       = first ? 0x01 : ((remaining <= max_data) ? 0x02 : 0x03);
        first = false;

        ByteBuffer pdu;
        pdu.data.resize(hdr_size + data_len);
        pdu.data[0] = static_cast<uint8_t>((si << 6) | (sn & 0x3F));
        if (hdr_size == hdr_with_so) {
            pdu.data[1] = static_cast<uint8_t>((offset >> 8) & 0xFF);
            pdu.data[2] = static_cast<uint8_t>(offset & 0xFF);
        }
        std::memcpy(pdu.data.data() + hdr_size, sdu.data.data() + offset, data_len);

        if (opt) pdus.push_back(std::move(pdu));  // OPT: move
        else     pdus.push_back(pdu);              // V1:  copy
        offset += data_len; remaining -= data_len;
    }
    return pdus;
}

// ============================================================
// UM RX — reassembly (shared parse, variant in try_reassemble)
// ============================================================
std::vector<ByteBuffer> RlcLayer::process_rx_um(const ByteBuffer& pdu) {
    std::vector<ByteBuffer> result;
    if (pdu.empty()) return result;

    uint8_t si = (pdu.data[0] >> 6) & 0x03;
    uint8_t sn = pdu.data[0] & 0x3F;

    RxSegment seg;
    seg.si = si;
    if (si == 0x00 || si == 0x01) {
        seg.offset = 0;
        seg.data.assign(pdu.data.begin() + 1, pdu.data.end());
    } else {
        seg.offset = (static_cast<uint16_t>(pdu.data[1]) << 8) | pdu.data[2];
        seg.data.assign(pdu.data.begin() + 3, pdu.data.end());
    }
    rx_buffer_[sn].push_back(std::move(seg));

    if (si == 0x00) {
        ByteBuffer sdu;
        if (config_.rlc_opt_level >= 1)
            sdu.data = std::move(rx_buffer_[sn][0].data);  // OPT: move
        else
            sdu.data = rx_buffer_[sn][0].data;             // V1:  copy
        rx_buffer_.erase(sn);
        result.push_back(std::move(sdu));
        return result;
    }

    ByteBuffer assembled = try_reassemble(sn);
    if (!assembled.empty()) {
        rx_buffer_.erase(sn);
        result.push_back(std::move(assembled));
    }
    return result;
}

// ---- UM reassembly helper — variant selected by rlc_opt_level ----
ByteBuffer RlcLayer::try_reassemble(uint8_t sn) {
    auto it = rx_buffer_.find(sn);
    if (it == rx_buffer_.end()) return ByteBuffer{};
    auto& segs = it->second;

    bool has_first = false, has_last = false;
    for (auto& s : segs) {
        if (s.si == 0x01) has_first = true;
        if (s.si == 0x02) has_last  = true;
    }
    if (!has_first || !has_last) return ByteBuffer{};

    const bool opt = (config_.rlc_opt_level >= 1);

    // OPT: skip sort for 2-segment first→last case; V1: always sort
    bool skip_sort = opt && segs.size() == 2 &&
                     segs[0].si == 0x01 && segs[1].si == 0x02;
    if (!skip_sort) {
        std::sort(segs.begin(), segs.end(),
                  [](const RxSegment& a, const RxSegment& b){ return a.offset < b.offset; });
    }

    size_t expected = 0;
    for (auto& s : segs) {
        if (s.offset != expected) return ByteBuffer{};
        expected = s.offset + s.data.size();
    }

    ByteBuffer sdu;
    if (opt) {
        // OPT: resize + memcpy (single allocation)
        size_t total = segs.back().offset + segs.back().data.size();
        sdu.data.resize(total);
        for (auto& s : segs)
            std::memcpy(sdu.data.data() + s.offset, s.data.data(), s.data.size());
    } else {
        // V1: insert loop (incremental growth)
        for (auto& s : segs)
            sdu.data.insert(sdu.data.end(), s.data.begin(), s.data.end());
    }
    return sdu;
}

// ============================================================
// AM TX — dispatches to baseline or optimized variant
// ============================================================
std::vector<ByteBuffer> RlcLayer::process_tx_am(const ByteBuffer& sdu) {
    std::vector<ByteBuffer> pdus;
    uint16_t sn = am_tx_next_sn_;

    const size_t   hdr_base    = 2;
    const size_t   hdr_with_so = 4;
    const uint32_t max_pdu     = config_.rlc_max_pdu_size;
    const bool     opt         = (config_.rlc_opt_level >= 1);

    // Single-PDU path
    if (sdu.size() + hdr_base <= max_pdu) {
        ByteBuffer pdu;
        pdu.data.resize(hdr_base + sdu.size());
        pdu.data[0] = AM_DC_BIT | ((sn >> 8) & AM_SN_HI_MASK);
        pdu.data[1] = static_cast<uint8_t>(sn & 0xFF);
        std::memcpy(pdu.data.data() + hdr_base, sdu.data.data(), sdu.size());
        if (opt) pdus.push_back(std::move(pdu));
        else     pdus.push_back(pdu);
    } else {
        // OPT: pre-allocate; V1: no reserve
        if (opt) {
            uint32_t n = (static_cast<uint32_t>(sdu.size()) +
                          (max_pdu - hdr_with_so - 1)) / (max_pdu - hdr_with_so);
            pdus.reserve(n);
        }

        size_t offset = 0, remaining = sdu.size();
        bool first = true;
        while (remaining > 0) {
            size_t  hdr_size = first ? hdr_base : hdr_with_so;
            size_t  max_data = max_pdu - hdr_size;
            size_t  data_len = std::min(remaining, max_data);
            uint8_t si       = first ? 0x01 : ((remaining <= max_data) ? 0x02 : 0x03);
            first = false;

            ByteBuffer pdu;
            pdu.data.resize(hdr_size + data_len);
            pdu.data[0] = AM_DC_BIT | (static_cast<uint8_t>(si) << AM_SI_SHIFT) |
                          ((sn >> 8) & AM_SN_HI_MASK);
            pdu.data[1] = static_cast<uint8_t>(sn & 0xFF);
            if (hdr_size == hdr_with_so) {
                pdu.data[2] = static_cast<uint8_t>((offset >> 8) & 0xFF);
                pdu.data[3] = static_cast<uint8_t>(offset & 0xFF);
            }
            std::memcpy(pdu.data.data() + hdr_size, sdu.data.data() + offset, data_len);

            if (opt) pdus.push_back(std::move(pdu));
            else     pdus.push_back(pdu);
            offset += data_len; remaining -= data_len;
        }
    }

    for (auto& p : pdus) retx_buf_[sn] = p;

    poll_pdu_count_++;
    if (poll_pdu_count_ >= config_.rlc_poll_pdu && !pdus.empty()) {
        pdus.back().data[0] |= AM_P_BIT;
        poll_pdu_count_ = 0;
        poll_sn_ = sn;
    }
    am_tx_next_sn_ = (am_tx_next_sn_ + 1) % 4096;
    return pdus;
}

// ============================================================
// STATUS PDU — build / parse (Member 3, shared by both variants)
// ============================================================
ByteBuffer RlcLayer::build_status_pdu(uint16_t ack_sn,
                                       const std::vector<uint16_t>& nack_sns) {
    ByteBuffer pdu;
    pdu.data.resize(3 + nack_sns.size() * 2);
    pdu.data[0] = static_cast<uint8_t>((ack_sn >> 8) & 0x0F);
    pdu.data[1] = static_cast<uint8_t>(ack_sn & 0xFF);
    if (nack_sns.empty()) {
        pdu.data[2] = 0x00;
    } else {
        pdu.data[2] = 0x80;
        for (size_t i = 0; i < nack_sns.size(); i++) {
            uint16_t nack = nack_sns[i];
            bool more = (i + 1 < nack_sns.size());
            pdu.data[3 + i * 2]     = static_cast<uint8_t>((nack >> 4) & 0xFF);
            pdu.data[3 + i * 2 + 1] = static_cast<uint8_t>(
                ((nack & 0x0F) << 4) | (more ? 0x08 : 0x00));
        }
    }
    return pdu;
}

RlcLayer::StatusPdu RlcLayer::parse_status_pdu(const ByteBuffer& pdu) {
    StatusPdu result;
    if (pdu.size() < 3) return result;
    result.ack_sn = (static_cast<uint16_t>(pdu.data[0] & 0x0F) << 8) | pdu.data[1];
    uint8_t e1 = (pdu.data[2] >> 7) & 0x01;
    size_t  pos = 3;
    while (e1 && pos + 1 < pdu.size()) {
        uint16_t nack = (static_cast<uint16_t>(pdu.data[pos]) << 4) |
                        ((pdu.data[pos + 1] >> 4) & 0x0F);
        result.nack_sns.push_back(nack);
        e1 = (pdu.data[pos + 1] >> 3) & 0x01;
        pos += 2;
    }
    result.valid = true;
    return result;
}

// ============================================================
// AM RX — process_rx_am() (Member 4)
// ============================================================
std::vector<ByteBuffer> RlcLayer::process_rx_am(const ByteBuffer& pdu) {
    std::vector<ByteBuffer> result;
    if (pdu.size() < 2) return result;

    // STATUS PDU (D/C=0)
    if ((pdu.data[0] & AM_DC_BIT) == 0) {
        StatusPdu sp = parse_status_pdu(pdu);
        if (!sp.valid) return result;

        std::unordered_map<uint16_t, bool> nack_set;
        for (uint16_t n : sp.nack_sns) nack_set[n] = true;

        auto it = retx_buf_.begin();
        while (it != retx_buf_.end()) {
            uint16_t dist = static_cast<uint16_t>((sp.ack_sn - it->first) & 0x0FFF);
            if (dist > 0 && dist < 2048 && nack_set.find(it->first) == nack_set.end())
                it = retx_buf_.erase(it);
            else ++it;
        }
        for (uint16_t nack_sn : sp.nack_sns) {
            auto rit = retx_buf_.find(nack_sn);
            if (rit != retx_buf_.end()) result.push_back(rit->second);
        }
        return result;
    }

    // Loss simulation (Req 6)
    double lr = config_.loss_rate;
    if (lr > 0.0 && lr < 1.0 &&
        static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX) < lr)
        return result;

    uint8_t  si = (pdu.data[0] >> AM_SI_SHIFT) & 0x03;
    uint16_t sn = (static_cast<uint16_t>(pdu.data[0] & AM_SN_HI_MASK) << 8) | pdu.data[1];

    AmRxSegment seg;
    seg.si = si;
    if (si == 0x00 || si == 0x01) {
        seg.offset = 0;
        seg.data.assign(pdu.data.begin() + 2, pdu.data.end());
    } else {
        if (pdu.size() < 4) return result;
        seg.offset = (static_cast<uint16_t>(pdu.data[2]) << 8) | pdu.data[3];
        seg.data.assign(pdu.data.begin() + 4, pdu.data.end());
    }

    const bool opt = (config_.rlc_opt_level >= 1);

    // Complete SDU path
    if (si == 0x00) {
        if (sn == rx_next_) {
            ByteBuffer sdu;
            if (opt) sdu.data = std::move(seg.data);
            else     sdu.data = seg.data;
            rx_next_ = (rx_next_ + 1) & 0x0FFF;
            result.push_back(std::move(sdu));
            // drain consecutive buffered SNs
            while (true) {
                auto bit = am_rx_buffer_.find(rx_next_);
                if (bit == am_rx_buffer_.end()) break;
                auto& bufs = bit->second;
                if (bufs.size() == 1 && bufs[0].si == 0x00) {
                    ByteBuffer csdu;
                    if (opt) csdu.data = std::move(bufs[0].data);
                    else     csdu.data = bufs[0].data;
                    am_rx_buffer_.erase(rx_next_);
                    rx_next_ = (rx_next_ + 1) & 0x0FFF;
                    result.push_back(std::move(csdu));
                    continue;
                }
                ByteBuffer assembled = try_reassemble_am(rx_next_);
                if (assembled.empty()) break;
                am_rx_buffer_.erase(rx_next_);
                rx_next_ = (rx_next_ + 1) & 0x0FFF;
                result.push_back(std::move(assembled));
            }
        } else {
            am_rx_buffer_[sn].push_back(std::move(seg));
            std::vector<uint16_t> nacks;
            for (uint16_t s = rx_next_; s != sn; s = (s + 1) & 0x0FFF) nacks.push_back(s);
            result.push_back(build_status_pdu(sn, nacks));
            rx_highest_status_ = sn;
        }
        return result;
    }

    // Segmented PDU
    am_rx_buffer_[sn].push_back(std::move(seg));
    if (sn == rx_next_) {
        ByteBuffer assembled = try_reassemble_am(sn);
        if (!assembled.empty()) {
            am_rx_buffer_.erase(sn);
            rx_next_ = (rx_next_ + 1) & 0x0FFF;
            result.push_back(std::move(assembled));
            while (true) {
                auto bit = am_rx_buffer_.find(rx_next_);
                if (bit == am_rx_buffer_.end()) break;
                auto& bufs = bit->second;
                if (bufs.size() == 1 && bufs[0].si == 0x00) {
                    ByteBuffer csdu;
                    if (opt) csdu.data = std::move(bufs[0].data);
                    else     csdu.data = bufs[0].data;
                    am_rx_buffer_.erase(rx_next_);
                    rx_next_ = (rx_next_ + 1) & 0x0FFF;
                    result.push_back(std::move(csdu));
                    continue;
                }
                ByteBuffer next_assembled = try_reassemble_am(rx_next_);
                if (next_assembled.empty()) break;
                am_rx_buffer_.erase(rx_next_);
                rx_next_ = (rx_next_ + 1) & 0x0FFF;
                result.push_back(std::move(next_assembled));
            }
        }
    } else {
        std::vector<uint16_t> nacks;
        for (uint16_t s = rx_next_; s != sn; s = (s + 1) & 0x0FFF) nacks.push_back(s);
        if (!nacks.empty()) {
            result.push_back(build_status_pdu((sn + 1) & 0x0FFF, nacks));
            rx_highest_status_ = sn;
        }
    }
    return result;
}

// ============================================================
// AM reassembly helper — variant selected by rlc_opt_level
// ============================================================
ByteBuffer RlcLayer::try_reassemble_am(uint16_t sn) {
    auto it = am_rx_buffer_.find(sn);
    if (it == am_rx_buffer_.end()) return ByteBuffer{};
    auto& segs = it->second;

    bool has_first = false, has_last = false;
    for (auto& s : segs) {
        if (s.si == 0x01) has_first = true;
        if (s.si == 0x02) has_last  = true;
    }
    if (!has_first || !has_last) return ByteBuffer{};

    const bool opt = (config_.rlc_opt_level >= 1);

    // OPT: skip sort for 2-segment first→last; V1: always sort
    bool skip_sort = opt && segs.size() == 2 &&
                     segs[0].si == 0x01 && segs[1].si == 0x02;
    if (!skip_sort) {
        std::sort(segs.begin(), segs.end(),
                  [](const AmRxSegment& a, const AmRxSegment& b){ return a.offset < b.offset; });
    }

    size_t expected = 0;
    for (auto& s : segs) {
        if (s.offset != expected) return ByteBuffer{};
        expected = s.offset + s.data.size();
    }

    ByteBuffer sdu;
    if (opt) {
        // OPT: resize + memcpy (single allocation)
        size_t total = segs.back().offset + segs.back().data.size();
        sdu.data.resize(total);
        for (auto& s : segs)
            std::memcpy(sdu.data.data() + s.offset, s.data.data(), s.data.size());
    } else {
        // V1: insert loop (incremental growth)
        for (auto& s : segs)
            sdu.data.insert(sdu.data.end(), s.data.begin(), s.data.end());
    }
    return sdu;
}
