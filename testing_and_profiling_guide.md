# Testing & Profiling Guide

This document explains how every pair tests their work, profiles their variants, and contributes to the final report. Read this before writing any profiling code.

---

## 1. The Two Phases of Testing

### Phase 1: Independent Testing (Days 1–3, each pair alone)

Each pair tests their layer in isolation using unit tests. You don't need other layers to be working or even merged. You feed known input into YOUR layer, measure it, and verify the output.

**What you're answering:** "Does my layer work correctly? Which variant performs better? Why?"

### Phase 2: Full Pipeline Testing (Days 4–5, everyone together)

After merging all branches, we run the full uplink → loopback → downlink pipeline with the selected "best" configuration from each pair. Pair D leads this.

**What you're answering:** "Does the whole system work end-to-end? What's the performance profile across all layers?"

---

## 2. How to Switch Between Variants (Config Flags)

Do NOT create separate files for each variant. Use config flags in `common.h`:

```cpp
// In common.h — each pair adds their flags with V1 defaults
struct Config {
    // === EXISTING V1 FIELDS (do not modify) ===
    uint32_t ip_packet_size       = 1400;
    uint32_t num_packets          = 10;
    uint8_t  pdcp_sn_length       = 12;
    bool     ciphering_enabled    = true;
    bool     integrity_enabled    = true;
    bool     compression_enabled  = false;
    uint8_t  rlc_mode             = 1;       // 0=TM, 1=UM, 2=AM
    uint8_t  rlc_sn_length        = 6;
    uint32_t rlc_max_pdu_size     = 500;
    uint8_t  logical_channel_id   = 4;
    uint32_t transport_block_size = 2048;

    // === PAIR A: PDCP variants ===
    uint8_t  cipher_algorithm     = 0;  // 0 = XOR (V1), 1 = AES-128-CTR
    uint8_t  integrity_algorithm  = 0;  // 0 = CRC32 (V1), 1 = HMAC-SHA256

    // === PAIR B: RLC variants ===
    // rlc_mode = 2 enables AM (already exists above)
    double   loss_rate            = 0.0;  // 0.0 = no loss, 0.1 = 10% loss

    // === PAIR C: MAC variants ===
    uint8_t  num_logical_channels = 1;  // 1 = V1 single channel
    bool     lcp_enabled          = false;
    bool     bsr_enabled          = false;
};
```

In your layer code, branch on these flags:

```cpp
void PdcpLayer::apply_cipher(std::vector<uint8_t>& data, uint32_t count) {
    if (!config_.ciphering_enabled) return;  // pass-through

    if (config_.cipher_algorithm == 0) {
        // V1: XOR stream cipher
        // ... existing code unchanged ...
    } else if (config_.cipher_algorithm == 1) {
        // NEW: AES-128-CTR via OpenSSL
        // ... new code ...
    }
}
```

This means you can test any variant by just changing a config value — no recompilation, no file swapping.

---

## 3. Independent Testing Per Pair

### How It Works

Each pair writes a profiling section inside their unit test file. The pattern is always the same:

1. Create a `Config` with your variant settings
2. Create your layer instance
3. Generate test data (a `ByteBuffer` with known content)
4. Run TX → RX in a loop (1000+ iterations)
5. Measure average time per operation
6. Verify correctness (output matches input after round-trip)
7. Print results in a consistent table format

### Template — Copy This Into Your Test File

```cpp
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cassert>

// Put this function in your test file (test_pdcp.cpp, test_rlc.cpp, etc.)
void profile_variants() {
    const int ITERATIONS = 1000;
    const std::vector<uint32_t> packet_sizes = {100, 500, 1000, 1400, 3000};

    std::cout << "\n========================================" << std::endl;
    std::cout << "PROFILING: [YOUR LAYER NAME]" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Table header
    std::cout << std::left
              << std::setw(12) << "PktSize"
              << std::setw(15) << "Variant"
              << std::setw(14) << "TX avg(μs)"
              << std::setw(14) << "RX avg(μs)"
              << std::setw(14) << "Overhead(B)"
              << std::setw(10) << "Pass"
              << std::endl;
    std::cout << std::string(79, '-') << std::endl;

    for (uint32_t pkt_size : packet_sizes) {
        // ---------- Variant A (V1 baseline) ----------
        {
            Config cfg;
            cfg.ip_packet_size = pkt_size;
            // cfg.cipher_algorithm = 0;  // V1 default
            YourLayer layer(cfg);

            // Generate test input
            ByteBuffer input;
            input.data.resize(pkt_size);
            for (size_t i = 0; i < pkt_size; i++)
                input.data[i] = static_cast<uint8_t>(i & 0xFF);

            double total_tx = 0, total_rx = 0;
            bool all_pass = true;
            size_t overhead = 0;

            for (int i = 0; i < ITERATIONS; i++) {
                auto t0 = std::chrono::high_resolution_clock::now();
                auto tx_out = layer.process_tx(input);
                auto t1 = std::chrono::high_resolution_clock::now();
                auto rx_out = layer.process_rx(tx_out);  // adapt for RLC (multiple PDUs)
                auto t2 = std::chrono::high_resolution_clock::now();

                total_tx += std::chrono::duration<double, std::micro>(t1 - t0).count();
                total_rx += std::chrono::duration<double, std::micro>(t2 - t1).count();

                if (rx_out.data != input.data) all_pass = false;
                if (i == 0) overhead = tx_out.size() - input.size();
            }

            std::cout << std::left
                      << std::setw(12) << pkt_size
                      << std::setw(15) << "V1-Baseline"
                      << std::setw(14) << std::fixed << std::setprecision(2)
                      << (total_tx / ITERATIONS)
                      << std::setw(14) << (total_rx / ITERATIONS)
                      << std::setw(14) << overhead
                      << std::setw(10) << (all_pass ? "PASS" : "FAIL")
                      << std::endl;
        }

        // ---------- Variant B (your optimization) ----------
        {
            Config cfg;
            cfg.ip_packet_size = pkt_size;
            // cfg.cipher_algorithm = 1;  // AES (example)
            YourLayer layer(cfg);

            // ... same loop as above, just different config ...
            // Print row with variant name "AES-CTR" or "AM-Mode" etc.
        }
    }

    std::cout << std::endl;
}
```

### Adapting This Template Per Pair

**Pair A (PDCP):** Test input is a raw IP packet (ByteBuffer). TX produces a PDCP PDU. RX recovers the original. Variants to compare: XOR vs AES cipher, CRC32 vs HMAC integrity, compression on vs off. Overhead = PDCP header + MAC-I bytes added.

**Pair B (RLC):** Test input is a PDCP PDU (ByteBuffer). TX produces a `vector<ByteBuffer>` (segments). RX takes segments one by one and eventually outputs the reassembled SDU. Adapt the template: TX returns multiple PDUs, feed them into RX one at a time. Variants: UM vs AM mode, 6-bit vs 12-bit SN. Also measure: number of segments produced, and for AM mode with loss simulation, number of retransmissions needed.

```cpp
// RLC-specific TX timing
auto t0 = std::chrono::high_resolution_clock::now();
auto rlc_pdus = rlc.process_tx(input);  // returns vector<ByteBuffer>
auto t1 = std::chrono::high_resolution_clock::now();

// RLC-specific RX timing — feed segments one by one
auto t2 = std::chrono::high_resolution_clock::now();
ByteBuffer reassembled;
for (auto& pdu : rlc_pdus) {
    auto result = rlc.process_rx(pdu);
    if (!result.empty()) reassembled = result[0];
}
auto t3 = std::chrono::high_resolution_clock::now();
```

Extra metrics for RLC: segment count per SDU, retransmission count (AM mode).

**Pair C (MAC):** Test input is one or more RLC PDUs (MAC SDUs). TX produces a Transport Block. RX parses it back to MAC SDUs. Variants: single LCID vs multi-LCID, LCP on vs off, BSR on vs off. Extra metric: efficiency = (total SDU bytes) / (transport block size) × 100%.

```cpp
// MAC efficiency calculation
double efficiency = 0;
size_t total_sdu_bytes = 0;
for (auto& sdu : sdus) total_sdu_bytes += sdu.size();
efficiency = (double)total_sdu_bytes / config.transport_block_size * 100.0;
```

---

## 4. What Each Pair Reports From Independent Testing

Each pair produces a short section (1–2 pages) containing:

### a) Variant Comparison Table

Example for PDCP:

```
┌──────────┬─────────────┬────────────┬────────────┬────────────┐
│ Pkt Size │ Variant     │ TX avg(μs) │ RX avg(μs) │ Overhead(B)│
├──────────┼─────────────┼────────────┼────────────┼────────────┤
│ 100      │ XOR/CRC32   │ 1.2        │ 1.1        │ 6          │
│ 100      │ AES/HMAC    │ 4.8        │ 4.5        │ 6          │
│ 1400     │ XOR/CRC32   │ 2.3        │ 2.1        │ 6          │
│ 1400     │ AES/HMAC    │ 8.7        │ 8.4        │ 6          │
│ 3000     │ XOR/CRC32   │ 4.1        │ 3.9        │ 6          │
│ 3000     │ AES/HMAC    │ 15.3       │ 14.8       │ 6          │
└──────────┴─────────────┴────────────┴────────────┴────────────┘
```

### b) Analysis Paragraph

Explain the numbers. Example: "AES-128-CTR is approximately 3.5x slower than XOR because it performs real block cipher operations for every 16-byte block. The overhead in bytes is identical (2-byte PDCP header + 4-byte MAC-I) because the header format doesn't change with the algorithm. Processing time scales linearly with packet size for both variants, confirming that the cipher operates per-byte."

### c) Recommendation

State which variant you selected for the final pipeline and why. Example: "We selected AES-128-CTR with HMAC-SHA256 for the final build because it provides realistic security as required by the 3GPP spec, and the performance cost is acceptable for our simulation."

---

## 5. Full Pipeline Testing (After Merge)

### 5.1 Configuration for Final Run

After all pairs merge, we agree on ONE "final" configuration:

```cpp
Config final_cfg;
// PDCP
final_cfg.cipher_algorithm    = 1;    // AES-128-CTR
final_cfg.integrity_algorithm = 1;    // HMAC-SHA256
final_cfg.compression_enabled = true;
final_cfg.pdcp_sn_length      = 12;

// RLC
final_cfg.rlc_mode            = 2;    // AM mode
final_cfg.rlc_sn_length       = 12;   // 12-bit SN for AM

// MAC
final_cfg.num_logical_channels = 2;
final_cfg.lcp_enabled          = true;
final_cfg.bsr_enabled          = true;
```

### 5.2 Profiling Matrix

Run the full pipeline with these parameter combinations:

| Parameter          | Values to test                          |
|--------------------|-----------------------------------------|
| Packet size (bytes)| 100, 500, 1000, 1400, 3000              |
| TB size (bytes)    | 256, 512, 1024, 2048, 4096, 8192        |
| Loss rate (AM only)| 0.0, 0.05, 0.10, 0.20                  |

That's 5 × 6 = 30 runs for the size matrix, plus 4 runs for the loss rate test.
Each run processes at least 50 packets for stable averages.

### 5.3 Running the Profiling Suite

Pair D prepares a script that automates all of this:

```bash
# scripts/run_profiling.sh — generates the full CSV
#!/bin/bash

OUTPUT="profiling_results.csv"

# Write CSV header
echo "packet_size,tb_size,loss_rate,pdcp_tx_us,rlc_tx_us,mac_tx_us,mac_rx_us,rlc_rx_us,pdcp_rx_us,total_tx_us,total_rx_us,num_retransmissions,mac_efficiency_pct,pass_fail" > $OUTPUT

# Size matrix (no loss)
for pkt in 100 500 1000 1400 3000; do
  for tb in 256 512 1024 2048 4096 8192; do
    ./5g_layer2 --packet-size $pkt --tb-size $tb --num-packets 50 \
                --cipher-algo 1 --integrity-algo 1 --compression on \
                --rlc-mode 2 --num-channels 2 --lcp on --bsr on \
                --csv-append $OUTPUT
  done
done

# Loss rate test (fixed sizes)
for loss in 0.0 0.05 0.10 0.20; do
  ./5g_layer2 --packet-size 1400 --tb-size 2048 --num-packets 100 \
              --rlc-mode 2 --loss-rate $loss \
              --csv-append $OUTPUT
done

echo "Profiling complete. Results in $OUTPUT"
```

### 5.4 Generating Charts

```bash
# After the profiling run
python3 scripts/generate_charts.py profiling_results.csv
# Outputs:
#   docs/charts/time_per_layer_bar.png
#   docs/charts/time_vs_tb_size.png
#   docs/charts/time_vs_pkt_size.png
#   docs/charts/retransmissions_vs_loss.png
#   docs/charts/mac_efficiency_vs_tb.png
```

### 5.5 Charts We Need for the Report

**Chart 1 — Stacked Bar: Time per Layer**
X-axis: each layer (PDCP-TX, RLC-TX, MAC-TX, MAC-RX, RLC-RX, PDCP-RX).
Y-axis: average time in microseconds.
Purpose: shows which layer is the bottleneck.

**Chart 2 — Line: Time vs Transport Block Size**
X-axis: TB size (256 to 8192).
One line per layer.
Purpose: shows how each layer scales with TB size.

**Chart 3 — Line: Time vs Packet Size**
X-axis: packet size (100 to 3000).
One line per layer.
Purpose: shows how each layer scales with data volume.

**Chart 4 — Bar: Retransmissions vs Loss Rate (RLC AM)**
X-axis: loss rate (0%, 5%, 10%, 20%).
Y-axis: average retransmissions per packet.
Second bar: data recovery rate (should be 100% for AM).
Purpose: demonstrates that AM mode handles loss.

**Chart 5 — Bar: MAC Efficiency vs TB Size**
X-axis: TB size.
Y-axis: percentage of TB used by actual data (vs headers + padding).
Purpose: shows overhead cost of the MAC layer.

---

## 6. Comparing V1 Baseline vs Optimized (Before/After)

Run the full profiling suite TWICE:

**Run 1: V1 defaults** (XOR, CRC32, UM mode, single LCID)
**Run 2: Optimized config** (AES, HMAC, AM mode, multi-LCID)

Produce a comparison table in the report:

```
┌──────────────┬──────────────┬──────────────┬──────────────┐
│ Metric       │ V1 Baseline  │ Optimized    │ Change       │
├──────────────┼──────────────┼──────────────┼──────────────┤
│ PDCP TX      │ 2.3 μs       │ 8.7 μs       │ +278% (AES)  │
│ PDCP RX      │ 2.1 μs       │ 8.4 μs       │ +300%        │
│ RLC TX       │ 1.5 μs       │ 2.1 μs       │ +40% (AM SN) │
│ RLC RX       │ 1.2 μs       │ 1.8 μs       │ +50%         │
│ MAC TX       │ 0.8 μs       │ 1.4 μs       │ +75% (LCP)   │
│ MAC RX       │ 0.7 μs       │ 1.1 μs       │ +57%         │
│ Total TX     │ 4.6 μs       │ 12.2 μs      │ +165%        │
│ Total RX     │ 4.0 μs       │ 11.3 μs      │ +183%        │
│ Data recovery│ N/A (no loss) │ 100% @ 10%   │ AM works     │
│ MAC efficiency│ 68%          │ 72%          │ +4% (LCP)    │
│ Security     │ None (XOR)   │ AES+HMAC     │ Real crypto  │
└──────────────┴──────────────┴──────────────┴──────────────┘
```

Then write a paragraph explaining the tradeoffs: "The optimized stack is slower due to stronger cryptography and richer protocol features, but it provides real security, loss recovery, and better channel utilization — all of which are required in a production 5G system."

---

## 7. Summary: Who Does What, When

| Day | Pairs A, B, C (independently)              | Pair D                                     |
|-----|--------------------------------------------|--------------------------------------------|
| 1–2 | Implement variants, run unit tests         | Build profiling CSV format, chart scripts   |
| 3   | Run independent profiling, pick best       | Test every pair's branch, report issues     |
|     | variant, write comparison table            |                                            |
| 4   | Help merge, fix integration issues,        | Lead merge, run full test suite,            |
|     | start writing report section               | run full profiling suite                    |
| 5   | Finish report section, review full report  | Generate charts, write intro/conclusion,    |
|     |                                            | assemble final report + user manual         |

---

## 8. Checklist Before Declaring "Done"

### Per Pair (before merge)
- [ ] All 23 original V1 tests still pass with default config
- [ ] New tests pass for all your variants
- [ ] Independent profiling table produced with at least 3 packet sizes
- [ ] Comparison paragraph written explaining the numbers
- [ ] Best variant selected and documented
- [ ] Code is commented with `// AI-assisted: reviewed by [Name]` where applicable
- [ ] All changes committed with proper `[LAYER]` prefixed messages

### Full Team (after merge)
- [ ] All original + new tests pass on merged `main`
- [ ] Full profiling suite runs without errors
- [ ] All 5 charts generated as PNG files
- [ ] V1 vs Optimized comparison table completed
- [ ] Report has all sections (intro, per-layer, profiling, conclusion, roles)
- [ ] User manual has build + run + test instructions
- [ ] Final code compiles cleanly with no warnings (`-Wall -Wextra`)
