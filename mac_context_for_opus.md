# Context File — MAC Layer (Pair C) for Claude Opus

Pass this file at the start of your session. It contains everything you need to understand
what was already built and what you are being asked to do next.

---

## Project Summary

8-person university team building a **5G NR Layer 2 protocol stack simulator in C++17**.
The stack has 4 layers: IP Generator → PDCP → RLC → MAC → (radio).
Each pair owns one layer. Pair C owns the MAC layer.

**The golden rule:** every IP packet that enters uplink must come out byte-identical from downlink.

---

## Pair C — Members and File Ownership

| Member | Files owned | Function ownership |
|---|---|---|
| Member 5 | `src/mac.cpp`, `include/mac.h`, `tests/test_mac.cpp` | `process_tx()`, `process_tx(multi)`, `process_rx_multi()`, LCP scheduler |
| Member 6 | `src/mac.cpp`, `include/mac.h`, `tests/test_mac.cpp` | BSR CE insertion, variable TB override, truncation logic |
| Both | `include/common.h` | Added `LcData` struct and new Config flags only |

**Do NOT touch:** `src/pdcp.cpp`, `src/rlc.cpp`, `src/main.cpp` (Pair D owns main.cpp)

---

## Key Rules (from AI_RULES.md)

1. **Never change existing public method signatures** in `.h` files without team approval
2. **All 23 original tests must pass** with default `Config` values at all times
3. **New features must be behind config flags** — defaults preserve V1 behaviour
4. **Never modify existing test functions** — only add new ones below them
5. **No premature abstraction** — three similar lines of code is better than a helper for 2 call sites
6. **Commit format:** `[MAC] Short description`

---

## Versions — What Changed in Each

### V1 — Baseline (original codebase, untouched)
- Single LCID (channel ID = 4)
- Fixed 2048-byte Transport Block
- `process_tx(vector<ByteBuffer>)` packs SDUs with subheaders, pads with LCID=63
- `process_rx(ByteBuffer)` parses subheaders, returns SDUs
- **Issue 1:** `tb.data.resize(2048, 0x00)` zero-fills the entire TB on every TX call
- **Issue 2:** No multi-channel support
- **Issue 3:** No scheduling — first-come first-served

### V2 — Multi-LCID (Member 5, Phase 1)
**What changed:**
- Added `struct LcData { uint8_t lcid; uint8_t priority; uint32_t pbr_bytes; vector<ByteBuffer> sdus; }` to `common.h`
- Added new overload `process_tx(vector<LcData> channels, size_t tb_size=0)` to `mac.h` + `mac.cpp`
- Old `process_tx(vector<ByteBuffer>)` became a **wrapper** — wraps input into a single `LcData` and delegates. All 23 original tests call this wrapper and are completely unaffected.
- Added `process_rx_multi(ByteBuffer)` — same as `process_rx` but returns `vector<pair<uint8_t, ByteBuffer>>` (LCID tag preserved per SDU)
- **Fixed TB zero-fill:** `resize(2048)` (no fill) + only zero the padding region. Saves ~TB_size wasted writes per call.
- Extracted `write_sdu()` static helper to avoid duplicating subheader-writing logic

**New Config flags added (all default=false/V1 behaviour):**
```cpp
bool lcp_enabled         = false;
bool bsr_enabled         = false;
uint8_t num_logical_channels = 1;
```

**LCID values used (per TS 38.321):**
- DRB LCIDs: 4, 5, 6, 7
- BSR (Short BSR): 61
- Padding: 63

### V3 — LCP Scheduling (Member 5, Phase 2)
**What changed inside `process_tx(vector<LcData>)`:**

When `lcp_enabled=true`, the scheduler follows TS 38.321 §5.4.3.1:
1. **Sort** channels by `priority` ascending (lower value = higher priority)
2. **PBR phase:** each channel sends SDUs up to its `pbr_bytes` quota in priority order
3. **Round-robin phase:** remaining SDUs cycle one per channel until TB full

When `lcp_enabled=false` (default): simple first-in-first-out, no sort, no quota.

**Why `vector<LcData>` instead of `map<uint8_t, ...>`:**
`std::map` iterates in key (LCID value) order, not priority order. Using a map would silently break LCP scheduling — the sort would be bypassed. `vector` + `std::sort` with lambda gives correct, explicit ordering.

**V3 micro-optimisations (also in Phase 2):**
- `pbr_rem` uses `reserve()` + `push_back()` instead of zero-init + overwrite (one write per element instead of two)
- `channels.size()` cached as `const size_t num_ch` before loop (avoids repeated size() calls)
- RX parse loop: checks `LCID_PADDING` (63) before `LCID_BSR` (61) — padding terminates every TB so it's the hot path; BSR is rare

### V3 + Member 6 — BSR CE + Variable TB
**What Member 6 added:**
- Short BSR MAC CE (LCID=61) inserted at front of TB when `bsr_enabled=true`
  - 2 bytes: `[0x3D]` (LCID=61 subheader) + `[LCG_ID(3 bits) | Buffer_Size_Index(5 bits)]`
  - Guard: `if (config_.bsr_enabled && pos + 2 <= effective_tb_size)`
- `process_tx(vector<LcData>, uint32_t tb_size_override)` overload — uses override instead of `config_.transport_block_size` for that one call
- Truncation: when TB too small, logs `[MAC TX] TB full. Truncating remaining SDUs.` and stops cleanly
- RX: when LCID=61 parsed, skips 1-byte BSR payload (`pos++`) and continues

**Bug Member 6 introduced (fixed by Member 5 in the integration commit):**
- Member 6 inserted BSR unconditionally (no `bsr_enabled` guard). This broke `test_padding` and `test_lcp_priority_ordering` because the unconditional 2-byte prefix shifted all SDU positions.
- Member 6 also replaced the LCP scheduling block with a plain loop, removing the sort and quota logic. This caused `test_lcp_priority_ordering` and `test_lcp_pbr_quota` to fail.
- Fix: restored `if (config_.bsr_enabled)` guard and re-integrated the full LCP scheduler.

---

## Current Test Status

```
12 / 12 MAC tests pass
23 / 23 total project tests pass (pdcp:6, rlc:5, mac:5, integration:7 — originals only)
```

The original 23 pass unchanged. The 7 new MAC tests are additions below them:
- `test_multi_lcid_mux_demux` (Member 5)
- `test_lcp_priority_ordering` (Member 5)
- `test_lcp_pbr_quota` (Member 5)
- `test_lcp_3channel_roundrobin` (Member 5) ← added after reviewer feedback: 2-channel round-robin test was not convincing enough; this uses 3 channels (LCID 4/5/6) to verify the RR cycle `4,5,6,4,5,6` is correct after the PBR phase
- `test_bsr_present_in_pdu` (Member 6)
- `test_variable_tb_override` (Member 6)
- `test_truncation_tb_too_small` (Member 6)

---

## Profiling Data (WSL2 — indicative only, noisy)

Measured on WSL2 (Linux 6.6 / Windows). WSL2 process scheduling adds ~0.2–1.5µs jitter.
**These numbers are directional, not precise.** Pair D will run on native Linux for the real report.

10,000 iterations per variant per packet size.

| Packet Size | Variant | TX avg (µs) | RX avg (µs) | Header Overhead |
|---|---|---|---|---|
| 100 B | V1-Baseline | ~0.79 | ~0.15 | 2–3 B |
| 100 B | Multi-LCID | ~0.55 | ~0.13 | 4 B |
| 100 B | LCP-On | ~0.62 | ~0.18 | 4 B |
| 100 B | BSR-On | ~0.80 | ~0.14 | 4 B |
| 500 B | V1-Baseline | ~0.75 | ~0.16 | 3 B |
| 500 B | Multi-LCID | ~0.60 | ~0.17 | 4 B |
| 500 B | LCP-On | ~0.65 | ~0.20 | 4 B |
| 500 B | BSR-On | ~0.65 | ~0.14 | 5 B |
| 1000 B | V1-Baseline | ~0.70 | ~0.14 | 3 B |
| 1000 B | Multi-LCID | ~0.68 | ~0.18 | 6 B |
| 1000 B | LCP-On | ~0.75 | ~0.22 | 6 B |
| 1000 B | BSR-On | ~0.70 | ~0.13 | 5 B |
| 1400 B | V1-Baseline | ~0.72 | ~0.17 | 3 B |
| 1400 B | Multi-LCID | ~0.62 | ~0.19 | 6 B |
| 1400 B | LCP-On | ~0.65 | ~0.24 | 6 B |
| 1400 B | BSR-On | ~1.20 | ~0.25 | 5 B |
| 3000 B | V1-Baseline | ~0.90 | ~0.26 | 3 B |
| 3000 B | Multi-LCID | ~0.85 | ~0.33 | 6 B |
| 3000 B | LCP-On | ~0.85 | ~0.35 | 6 B |
| 3000 B | BSR-On | ~1.20 | ~0.23 | 5 B |

**TB efficiency at 1400B packet / 2048B TB: 68.4%** (648 bytes wasted on padding)

**Key insight:** The MAC layer's value is in *features added*, not raw speedup. Unlike RLC which showed
1.3–1.5× TX speedup from algorithmic optimization, MAC's improvements are:
1. Multi-channel multiplexing (V1 had 1 channel; V3 supports many)
2. Standards-compliant LCP scheduling (TS 38.321 §5.4.3.1)
3. Buffer Status Reporting (TS 38.321 §6.1.3.1)
4. Variable TB size per call
The TB zero-fill fix is the only raw performance gain — it eliminates O(TB_size) zero-writes per TX call.

---

## Git State

Branch: `feat/mac`
Commits (most recent first):
- `88d37e3` [MAC] V3: micro-optimizations in process_rx and LCP scheduling
- `dbcd49c` [MAC] Fix Member 6 integration + complete Pair C report
- `8723aae` [MAC] Member 6: BSR CE, Variable TB size, Truncation
- `82ff01e` [MAC] Add Member 5 implementation report
- `ff1a879` [MAC] Add functional tests for multi-LCID and LCP
- `2f235f7` [MAC] Add LCP scheduling + post-change profiling variants
- `e6ffc30` [MAC] Add multi-LCID mux/demux + fix TB zero-fill
- `e1798d0` [MAC] Add baseline profiling function

Branch has been pushed. PR open against `main`. Merges last (after feat/testing, feat/pdcp, feat/rlc).

---

## What You Are Being Asked to Do

**Goal:** Create an interactive HTML benchmark visualization for the MAC layer,
similar in style to `rlc_benchmark_results_v3.html` (provided separately).

**Use the profiling data table above.**

**Tabs to include:**
1. **Verdict** — summary metrics + key finding paragraph + TX bar chart (all 4 variants)
2. **TX / RX Latency** — line charts showing all 4 variants across packet sizes
3. **Speedup** — bar chart of Multi-LCID/LCP/BSR speedup ratios vs V1 (or note if not meaningful)
4. **Full Table** — green = best per row, red = worst
5. **By Version** — toggle between V2 (Multi-LCID), V3 (LCP), V3+M6 (BSR) with notes

**Important notes for the HTML:**
- Add a prominent warning: "Measured on WSL2 — absolute values have ±0.3–1.5µs jitter. Trends are directional."
- The story is feature additions, not raw speedup — frame the verdict accordingly
- Use the same color/font style as the RLC HTML
- File should be standalone (no external dependencies except Chart.js CDN)
