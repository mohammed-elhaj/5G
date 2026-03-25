#pragma once

#include "common.h"
#include <vector>

class MacLayer {
public:
    explicit MacLayer(const Config& cfg);

    // Multiplex SDUs from a single logical channel into a transport block (TS 38.321 §6.1.2)
    ByteBuffer process_tx(const std::vector<ByteBuffer>& sdus, size_t tb_size = 0);

    // Multiplex SDUs from multiple logical channels with optional LCP scheduling (TS 38.321 §5.4.3.1)
    ByteBuffer process_tx(std::vector<LcData> channels, size_t tb_size = 0);

    // Demultiplex transport block into SDUs, discarding LCID tags (TS 38.321 §6.1.2)
    std::vector<ByteBuffer> process_rx(const ByteBuffer& transport_block);

    // Demultiplex transport block into (LCID, SDU) pairs for multi-channel reception (TS 38.321 §6.1.2)
    std::vector<std::pair<uint8_t, ByteBuffer>> process_rx_multi(const ByteBuffer& transport_block);

    void reset();

private:
    Config config_;

    // MAC CE LCIDs per TS 38.321 Table 6.2.1-1
    static constexpr uint8_t LCID_PADDING = 63;
    static constexpr uint8_t LCID_BSR     = 61;
};
