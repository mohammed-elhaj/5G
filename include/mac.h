#pragma once

#include "common.h"
#include <vector>

class MacLayer {
public:
    explicit MacLayer(const Config& cfg);

    ByteBuffer process_tx(const std::vector<ByteBuffer>& sdus, size_t tb_size = 0);

    ByteBuffer process_tx(std::vector<LcData> channels, size_t tb_size = 0);

    std::vector<ByteBuffer> process_rx(const ByteBuffer& transport_block);

    std::vector<std::pair<uint8_t, ByteBuffer>> process_rx_multi(const ByteBuffer& transport_block);

    void reset();

private:
    Config config_;

    static constexpr uint8_t LCID_PADDING = 63;

    static constexpr uint8_t LCID_BSR     = 61;
};
