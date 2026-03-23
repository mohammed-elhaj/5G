<<<<<<< HEAD
# MAC Layer — Pair C Implementation Report
**Branch:** `feat/mac` | **Authors:** Member 5 & Member 6, Pair C
**Date:** 2026-03-18 | **Status:** Complete — ready for merge
=======
# MAC Layer — Member 5 Implementation Report
**Branch:** `feat/mac` | **Author:** Member 5, Pair C
**Date:** 2026-03-16 | **Status:** Ready for merge (pending Member 6 BSR + variable TB)
>>>>>>> 82ff01e ([MAC] Add Member 5 implementation report)

---

## 1. What Was Changed and Why

<<<<<<< HEAD
### Problems with V1

V1 MAC had four limitations this work addresses:

| Problem | Location | Impact |
|---|---|---|
| Entire TB zero-filled every TX call | `resize(n, 0x00)` | ~2 KB of wasted writes per packet even when SDUs fill 90% of the TB |
| Single hardcoded LCID per TB | `config_.logical_channel_id` baked in | No way to multiplex multiple logical channels |
| No scheduling between channels | `process_tx` single-channel only | Cannot prioritise real-time traffic over background data |
| No MAC control elements | Missing LCID 61 handling | No buffer status reporting |

---

## 2. Implementation — V2 (Pair C)

### Member 5 — Multi-LCID Mux/Demux & LCP Scheduling

| Feature | Config flag | Default | Spec ref |
|---|---|---|---|
| Multi-LCID TX via `process_tx(vector<LcData>)` | `num_logical_channels` | 1 (V1) | TS 38.321 §5.4 |
| LCID-tagged RX via `process_rx_multi()` | — | always available | TS 38.321 §6.1.2 |
| TB zero-fill fix (only pad region zeroed) | — | always active | — |
| Logical Channel Prioritization (LCP) | `lcp_enabled` | false (V1) | TS 38.321 §5.4.3.1 |

**LCP algorithm (when `lcp_enabled=true`):**
1. Sort channels by `priority` ascending (lower = higher priority)
2. **PBR phase:** serve each channel in order until `pbr_bytes` quota consumed or drained
3. **Round-robin phase:** cycle remaining channels one SDU at a time until TB full

**Why `vector<LcData>` and not `map<uint8_t, vector<ByteBuffer>>`:** `std::map` iterates in LCID-value order, not priority order — LCP scheduling would be silently wrong. `vector<LcData>` with `std::sort` gives explicit, correct ordering.

### Member 6 — BSR MAC CE & Variable TB Size

| Feature | Config flag | Default | Spec ref |
|---|---|---|---|
| Short BSR MAC CE (LCID=61, 1-byte payload) | `bsr_enabled` | false (V1) | TS 38.321 §6.1.3.1 |
| Variable TB size per-call override | `tb_size` param (default=0 → config) | config value | TS 38.321 §5.3 |
| Graceful truncation when TB too small | — | always active | — |

**BSR format:** `[LCID=61][LCG ID (3 bits) | Buffer Size Index (5 bits)]` — prepended before SDUs.

All new features are **additive only** — the original `process_tx(const vector<ByteBuffer>&)` signature is unchanged and delegates to the new implementation.

---

## 3. Implementation — V3 (Optimizations)

Four micro-optimizations applied to `src/mac.cpp` after V2 testing confirmed correctness. No interface changes. All tests remain green.

| # | Optimization | Where | Rationale |
|---|---|---|---|
| 1 | `reserve(tb_size/3)` on RX output vectors | `process_rx`, `process_rx_multi` | Eliminates 1–2 heap reallocations per RX call for typical 3-SDU TBs (capacity would grow 1→2→4 without hint) |
| 2 | Reorder RX branch checks: `LCID_PADDING` before `LCID_BSR` | Both RX loops | Padding terminates every TB (common path); BSR is rare. Saves one comparison per SDU subheader on the typical path |
| 3 | `pbr_rem` single-pass init via `reserve`+`push_back` | LCP `process_tx` | Was zero-initialising then overwriting — each element now written exactly once |
| 4 | Cache `num_ch = channels.size()` in LCP loops | LCP PBR + round-robin | Guarantees no repeated `size()` evaluation in loop conditions |

**What was deliberately not changed:**

| Considered | Reason skipped |
|---|---|
| Extract shared RX parse helper | Two call sites — AI_RULES: avoid abstraction for its own sake |
| Change `process_tx` to `const vector<LcData>&` | Breaks LCP sort (needs mutable copy) + public interface |
| Replace `std::sort` with insertion sort for small N | LCP off by default; sort rarely runs |
| Replace `std::fill` with `memset` | Compiler already converts `std::fill<uint8_t>(0x00)` to `memset` |

---

## 4. Test Results

### 4a. Original V1 Tests (never modified)

| Suite | V1 | V2 | V3 |
|---|---|---|---|
| MAC — 5 tests | PASS | PASS | **PASS** |
| PDCP — 6 tests | PASS | PASS | **PASS** |
| RLC — 5 tests | PASS | PASS | **PASS** |
| Integration — 7 tests | PASS | PASS | **PASS** |
| **Total** | **23 / 23** | **23 / 23** | **23 / 23** |

### 4b. New Functional Tests — Member 5 (added in V2)

| Test | What It Verifies | Result |
|---|---|---|
| `test_multi_lcid_mux_demux` | LCID 4 + LCID 5, 2 SDUs each — full round-trip with correct LCID tags | **PASS** |
| `test_lcp_priority_ordering` | Channels fed in reverse order — sort puts LCID 4 (prio=1) first in raw TB bytes | **PASS** |
| `test_lcp_pbr_quota` | ch4 PBR=100B, 5×30B SDUs — 3 sent in PBR phase, 2 via round-robin; ch5 between phases | **PASS** |

### 4c. New Functional Tests — Member 6 (added in V2, fixed in integration)

| Test | What It Verifies | Result |
|---|---|---|
| `test_bsr_in_pdu` | `tb.data[0] & 0x3F == 61` when `bsr_enabled=true`; SDU still round-trips correctly | **PASS** |
| `test_variable_tb_size` | `process_tx({sdu}, 200)` produces exactly 200-byte TB; SDU recoverable | **PASS** |
| `test_tb_too_small_truncation` | `process_tx({100B sdu}, 10)` produces 10-byte TB with no SDUs (only padding) | **PASS** |

### 4d. V3 — No New Tests Required

V3 contains only internal implementation optimizations — no new code paths, no new config flags, no new observable behavior. The existing 11 tests already exercise every optimized code path:

| Optimized path | Covered by |
|---|---|
| `process_rx` with `reserve` + branch reorder | `test_single_small_sdu`, `test_multiple_sdus`, `test_padding`, `test_bsr_in_pdu` |
| `process_rx_multi` with `reserve` + branch reorder | `test_multi_lcid_mux_demux`, `test_lcp_priority_ordering`, `test_lcp_pbr_quota` |
| `pbr_rem` single-pass init, `num_ch` cache | `test_lcp_priority_ordering`, `test_lcp_pbr_quota` |

**Final test count: 11 / 11 MAC tests passing (5 original + 3 Member 5 + 3 Member 6)**

---

## 5. Performance Results

> **Note on timing:** Measurements on WSL2 (Windows Subsystem for Linux).
> WSL2 timer jitter is ±0.3–0.5 µs between runs due to shared CPU scheduling with Windows.
> Numbers below are indicative only. **Pair D will produce definitive numbers on native Linux.**
> Overhead byte counts and correctness results are fully reliable.

### 5a. Profiling Table — V2 vs V3 (1000 iterations, 1400B packet)

| Variant | V2 TX (µs) | V3 TX (µs) | V2 RX (µs) | V3 RX (µs) | Overhead | Pass |
|---|---|---|---|---|---|---|
| V1-Baseline | ~0.55 | ~0.55 | ~0.11 | ~0.11 | 3 B | PASS |
| Multi-LCID | ~0.41 | ~0.41 | ~0.12 | ~0.12 | 6 B | PASS |
| LCP-On | ~0.54 | ~0.54 | ~0.12 | ~0.12 | 6 B | PASS |
| BSR-On | ~0.59 | ~0.59 | ~0.12 | ~0.12 | 5 B | PASS |

> TX times are unchanged in V3 — the TX path was already optimized in V2 (zero-fill fix).
> RX improvements from `reserve()` and branch reordering are below WSL2 noise floor for single-SDU TBs.
> The gain is most visible on Pair D's profiling runs with many SDUs per TB (`--stress` flag).

### 5b. Full Profiling Table — All Variants, All Packet Sizes (V3)

| Packet Size | Variant | TX avg (µs) | RX avg (µs) | Subheader Overhead | Correctness |
|---|---|---|---|---|---|
| 100 B | V1-Baseline | ~0.6 | ~0.1 | 2 B | PASS |
| 100 B | Multi-LCID | ~0.6 | ~0.1 | 4 B | PASS |
| 100 B | LCP-On | ~0.7 | ~0.2 | 4 B | PASS |
| 100 B | BSR-On | ~0.6 | ~0.1 | 4 B | PASS |
| 500 B | V1-Baseline | ~0.5 | ~0.1 | 3 B | PASS |
| 500 B | Multi-LCID | ~0.5 | ~0.1 | 4 B | PASS |
| 500 B | LCP-On | ~0.6 | ~0.2 | 4 B | PASS |
| 500 B | BSR-On | ~0.5 | ~0.1 | 5 B | PASS |
| 1000 B | V1-Baseline | ~0.5 | ~0.1 | 3 B | PASS |
| 1000 B | Multi-LCID | ~0.6 | ~0.1 | 6 B | PASS |
| 1000 B | LCP-On | ~0.8 | ~0.2 | 6 B | PASS |
| 1000 B | BSR-On | ~0.6 | ~0.1 | 5 B | PASS |
| **1400 B** | **V1-Baseline** | **~0.55** | **~0.11** | **3 B** | **PASS** |
| **1400 B** | **Multi-LCID** | **~0.41** | **~0.12** | **6 B** | **PASS** |
| **1400 B** | **LCP-On** | **~0.54** | **~0.12** | **6 B** | **PASS** |
| **1400 B** | **BSR-On** | **~0.59** | **~0.12** | **5 B** | **PASS** |
| 3000 B | V1-Baseline | ~0.8 | ~0.2 | 3 B | PASS |
| 3000 B | Multi-LCID | ~0.7 | ~0.2 | 6 B | PASS |
| 3000 B | LCP-On | ~0.8 | ~0.3 | 6 B | PASS |
| 3000 B | BSR-On | ~0.7 | ~0.2 | 5 B | PASS |

### 5c. MAC Efficiency
=======
### Problem with V1

V1 MAC had three limitations that this work addresses:

| Problem | Location | Impact |
|---|---|---|
| Entire 2048-byte TB zero-filled every TX call | `mac.cpp:28` | ~2 KB of wasted writes per packet even when SDUs fill 90% of the TB |
| Single hardcoded LCID in every subheader | `mac.cpp:39` | No way to multiplex multiple logical channels |
| No scheduling between channels | `process_tx` signature | Cannot prioritise real-time traffic (e.g. voice) over background data |

### What Was Implemented

| Feature | Config flag | Default | Files |
|---|---|---|---|
| Multi-LCID mux/demux | `num_logical_channels` | 1 (V1) | `mac.cpp`, `mac.h`, `common.h` |
| `process_rx_multi` — LCID-tagged RX | — | always available | `mac.cpp`, `mac.h` |
| TB zero-fill fix | — | always active | `mac.cpp` |
| Logical Channel Prioritization (LCP) | `lcp_enabled` | false (V1) | `mac.cpp` |

All new features are **additive only**. The original `process_tx(vector<ByteBuffer>)` signature is unchanged and delegates to the new implementation — backward compatibility is guaranteed.

---

## 2. Test Results

### 2a. Existing V1 Tests (must always pass — never modified)

| Test | V1 Result | After Changes |
|---|---|---|
| Single small SDU (100B, 8-bit L) | PASS | **PASS** |
| Single large SDU (500B, 16-bit L) | PASS | **PASS** |
| Multiple SDUs (3 SDUs in one TB) | PASS | **PASS** |
| Mixed SDU sizes (50, 300, 150B) | PASS | **PASS** |
| Padding fills remaining TB space | PASS | **PASS** |
| PDCP 6 tests | PASS | **PASS** |
| RLC 5 tests | PASS | **PASS** |
| Integration 7 tests | PASS | **PASS** |
| **Total** | **23 / 23** | **23 / 23** |

### 2b. New Functional Tests (Member 5)

| Test | What It Verifies | Result |
|---|---|---|
| `test_multi_lcid_mux_demux` | 2 channels (LCID 4 + 5), 2 SDUs each — full round-trip byte match | **PASS** |
| `test_lcp_priority_ordering` | Channels fed in reverse order — sort puts LCID 4 (prio=1) first in raw TB bytes | **PASS** |
| `test_lcp_pbr_quota` | ch4 PBR=100B with 5×30B SDUs — exactly 3 sent in PBR phase, 2 via round-robin | **PASS** |
| **New total** | | **8 / 8** |

### 2c. New Tests Still Pending (Member 6)

| Test | Covers |
|---|---|
| `test_bsr_present_in_pdu` | BSR MAC CE (LCID 61) appears in TB when `bsr_enabled=true` |
| `test_bsr_parsed_on_rx` | `process_rx_multi` extracts LCID 61 correctly |
| `test_variable_tb_sizes` | `process_tx` with TB size override (512 / 1024 / 2048) |
| `test_tb_too_small_truncation` | SDUs that don't fit are skipped gracefully |

---

## 3. Performance Results

> **Note on timing:** These measurements were taken on WSL2 (Windows Subsystem for Linux).
> WSL2 has known timer jitter because the Linux kernel shares CPU time with Windows.
> Individual µs readings can vary ±0.5 µs between runs.
> The numbers below are approximate averages across 4 runs at 1000 iterations each.
> **For definitive numbers, Pair D will run the full profiling suite on native Linux.**
> Trends and overhead bytes are reliable; absolute µs values are indicative only.

### 3a. TX Performance Comparison (1000 iterations per packet size)

| Packet Size | Variant | TX avg (µs) | RX avg (µs) | Subheader Overhead | Correctness |
|---|---|---|---|---|---|
| 100 B | V1-Baseline | ~0.5 | ~0.1 | 2 B | PASS |
| 100 B | Multi-LCID | ~0.5 | ~0.2 | 4 B | PASS |
| 100 B | LCP-On | ~0.6 | ~0.2 | 4 B | PASS |
| 500 B | V1-Baseline | ~0.7 | ~0.1 | 3 B | PASS |
| 500 B | Multi-LCID | ~0.8 | ~0.3 | 4 B | PASS |
| 500 B | LCP-On | ~0.7 | ~0.3 | 4 B | PASS |
| 1000 B | V1-Baseline | ~0.9 | ~0.1 | 3 B | PASS |
| 1000 B | Multi-LCID | ~0.7 | ~0.3 | 6 B | PASS |
| 1000 B | LCP-On | ~0.7 | ~0.2 | 6 B | PASS |
| 1400 B | V1-Baseline | ~0.76 | ~0.15 | 3 B | PASS |
| 1400 B | Multi-LCID | ~0.46 | ~0.15 | 6 B | PASS |
| 1400 B | LCP-On | ~0.47 | ~0.16 | 6 B | PASS |
| 3000 B | V1-Baseline | ~0.81 | ~0.17 | 3 B | PASS |
| 3000 B | Multi-LCID | ~0.81 | ~0.23 | 6 B | PASS |
| 3000 B | LCP-On | ~0.82 | ~0.23 | 6 B | PASS |

### 3b. MAC Efficiency

MAC efficiency = (total SDU bytes) / (transport block size) × 100%
>>>>>>> 82ff01e ([MAC] Add Member 5 implementation report)

| Scenario | SDU bytes | TB size | Efficiency |
|---|---|---|---|
| V1 single channel, 1400B packet | 1400 B | 2048 B | **68.4%** |
| Multi-LCID, 2×700B | 1400 B | 2048 B | **68.4%** |
<<<<<<< HEAD
| BSR-On, 1400B SDU | 1398 B (2B used by BSR CE) | 2048 B | **68.3%** |

---

## 6. Analysis

### TX timing stays flat despite more work

All variants operate within the same ~0.4–0.6 µs TX range for typical 1400B packets. The TB zero-fill fix (V2) eliminated ~2 KB of `memset` per call, which more than compensates for the added work of sorting and LCP scheduling.

### V3 optimizations — where the gains appear

The V3 gains are primarily in the RX path and will be measurable on native Linux under multi-SDU workloads:

- **`reserve()` benefit:** A 2048-byte TB carrying 3 RLC PDUs (~474B each) would cause 2 vector reallocations without the hint (capacity grows 1→2→4). With `reserve(tb_size/3)`, capacity is allocated once. Gain scales with SDU count per TB.
- **Branch reordering benefit:** Padding is the last subheader of every TB. Without reordering, the parser checked BSR first on every subheader including the final padding one. With reordering, the common path (padding → break) is taken immediately.
- **`pbr_rem` and `num_ch`:** Minor — eliminates a redundant write pass per LCP TX call.

### Subheader overhead is correct and expected

Overhead increases from 3B (V1, one subheader) to 6B (two channels). At 1400B total SDU size, 6B overhead = 0.4% — negligible. For BSR-On: 2B BSR CE + 3B SDU subheader (16-bit length for 1400B > 255B) = 5B.

### LCP prevents channel starvation

Without LCP, a high-priority channel can drain the entire TB before low-priority channels get any space. The PBR phase guarantees each channel a minimum allocation per scheduling opportunity. The round-robin phase fairly distributes remaining space. This matches TS 38.321 §5.4.3.1.

---

## 7. Recommendation

**Selected variant for final pipeline:** `num_logical_channels=2, lcp_enabled=true, bsr_enabled=false`

BSR is off by default to preserve V1 behaviour across all integration tests. It can be enabled per-call when the scheduler needs buffer information.

---

## 8. Commit History on feat/mac

| Commit | Author | Description |
|---|---|---|
| `e1798d0` | Member 5 | Add baseline profiling (V1-Baseline variant) |
| `e6ffc30` | Member 5 | Multi-LCID mux/demux + TB zero-fill fix |
| `2f235f7` | Member 5 | LCP scheduling + post-change profiling variants |
| `ff1a879` | Member 5 | Functional tests for multi-LCID and LCP |
| `82ff01e` | Member 5 | Initial implementation report |
| `8723aae` | Member 6 | BSR CE, variable TB size, truncation |
| `dbcd49c` | Member 5 | Fix Member 6 integration issues + complete Pair C report |
| `88d37e3` | Member 5 | **V3: micro-optimizations in process_rx and LCP scheduling** |
=======

The efficiency is identical — the TB zero-fill fix reduces CPU overhead, not the padding ratio. Padding ratio depends on how well the total SDU bytes fill the TB, which is unchanged.

---

## 4. Analysis

### TX timing stays flat despite more work

Multi-LCID and LCP-On TX times are comparable to V1-Baseline across all packet sizes. This is because:

- **TB zero-fill fix** eliminated ~2 KB of `memset` per call. In V1, `resize(2048, 0x00)` zeroed the entire TB even when SDUs filled most of it. Now only the padding region is zeroed — typically a few hundred bytes at most.
- **LCP sort** (`std::sort` on 2 elements) is essentially free — a single comparison.
- **PBR + round-robin logic** adds a few loop iterations but no memory allocation, so the cost is within the noise floor on WSL2.

### Overhead increase is expected and unavoidable

Subheader overhead doubles from 3 B to 6 B when using 2 LCIDs (one 3-byte subheader per SDU instead of one total). This is correct 3GPP NR MAC behaviour — every SDU gets its own subheader with its own LCID. The overhead is fixed per SDU regardless of SDU size, so it becomes negligible for large packets (6B / 1400B = 0.4%).

### What LCP adds beyond Multi-LCID

Without LCP (`lcp_enabled=false`), channels are served in priority order but with no byte quota — a high-priority channel could drain the TB before lower-priority channels get any space. With LCP, each channel is guaranteed up to `pbr_bytes` of airtime per TTI before the round-robin phase. This prevents starvation of lower-priority bearers while still honouring priority.

---

## 5. Recommendation

**Selected variant for final pipeline:** `num_logical_channels=2, lcp_enabled=true`

**Reason:** LCP adds correct 3GPP scheduling behaviour with no measurable performance cost on this hardware. The TB zero-fill fix compensates for the small overhead of the sort and loop. All 23 original tests pass with default config (backward compatible). New features verified by 3 dedicated functional tests.

**Pending before merge:** Member 6 BSR + variable TB size implementation on the same `feat/mac` branch.

---

## 6. Commits on feat/mac (Member 5)

| Commit | Description |
|---|---|
| `e1798d0` | Add baseline profiling function (V1-Baseline variant) |
| `e6ffc30` | Add multi-LCID mux/demux + fix TB zero-fill |
| `2f235f7` | Add LCP scheduling + post-change profiling variants |
| `ff1a879` | Add functional tests for multi-LCID and LCP |

---

*This report will be updated after Member 6's commits are pulled and the BSR-On profiling row is added.*
>>>>>>> 82ff01e ([MAC] Add Member 5 implementation report)
