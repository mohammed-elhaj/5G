#pragma once
// ============================================================
// common.h — Shared types for the 5G NR Layer 2 simulator
//
// Defines ByteBuffer (the universal data container passed between
// layers), Config (all tuneable parameters set once at startup),
// and LayerProfile (per-layer timing results).
// ============================================================

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

// ============================================================
// Core buffer type used everywhere.
// Every PDU / SDU / Transport Block is just a ByteBuffer.
// ============================================================
struct ByteBuffer {
    std::vector<uint8_t> data;

    size_t size() const { return data.size(); }
    bool   empty() const { return data.empty(); }
};

// ============================================================
// Configuration — set once at startup, read by all layers.
// Default values match a typical single-bearer uplink scenario.
// ============================================================
struct Config {
    // --- IP Generator ---
    uint32_t ip_packet_size      = 1400;   // bytes (typical MTU minus IP/UDP headers)
    uint32_t num_packets         = 10;

    // --- PDCP (TS 38.323 simplified) ---
    uint8_t  pdcp_sn_length      = 12;     // 12 or 18 bits
    bool     ciphering_enabled   = true;
    bool     integrity_enabled   = true;
    bool     compression_enabled = false;   // V1: skip ROHC
    uint8_t  cipher_key[16]      = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                                    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10};
    uint8_t  integrity_key[16]   = {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                                    0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20};

    // --- RLC (TS 38.322, UM mode) ---
    uint8_t  rlc_mode            = 1;       // 0=TM, 1=UM, 2=AM  (V1: UM only)
    uint8_t  rlc_sn_length       = 6;       // 6 or 12 bits for UM
    uint32_t rlc_max_pdu_size    = 500;     // simulates MAC grant size

    // --- MAC (TS 38.321 simplified) ---
    uint8_t  logical_channel_id  = 4;       // DTCH (LCID 4 for first DRB)
    uint32_t transport_block_size = 2048;   // fixed TB size for V1

    // --- RLC AM extensions (Pair B, Member 3) ---
    uint8_t  rlc_am_sn_length    = 12;     // SN bit-width for AM mode (always 12 per TS 38.322)
    uint16_t rlc_poll_pdu        = 64;     // Poll every N PDUs (P-bit trigger threshold)

    // --- RLC loss simulation (Pair B, Member 4) ---
    double   loss_rate           = 0.0;    // PDU drop probability [0.0, 1.0); 0.0 = no loss

    // --- RLC optimization level (Pair B) ---
    // 0 = V1 unoptimized baseline (no reserve, copy push_back, insert-loop reassembly, always sort)
    // 1 = optimized (reserve, std::move, resize+memcpy, 2-segment fast path)
    // Default 0 preserves V1 behavior for all existing tests.
    uint8_t  rlc_opt_level       = 1;

    // --- Benchmark CSV output (Pair B) ---
    // If non-empty, profile_variants() writes one row per iteration to this file.
    // Columns: pkt_size, variant, iteration, tx_us, rx_us, pass
    // Default empty = no CSV output.
    std::string benchmark_csv_path = "";
};

// ============================================================
// Per-layer timing result — collected during the loopback run
// ============================================================
struct LayerProfile {
    std::string layer_name;
    double      uplink_us   = 0.0;   // microseconds spent in TX path
    double      downlink_us = 0.0;   // microseconds spent in RX path
};
