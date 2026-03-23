# Team Task Assignments — 8 Members, Parallel Work

## Team Structure Overview

We split into **4 pairs**, each owning one area of the protocol stack. Every pair works **independently on their own Git branch** — no pair blocks another. After all optimization work is merged, the **entire team** collaborates on profiling, the report, and the user manual.

```
┌─────────────────────────────────────────────────────────────────┐
│                    PARALLEL PHASE (Days 1–3)                    │
│                                                                 │
│  Pair A          Pair B          Pair C          Pair D         │
│  PDCP            RLC             MAC             Testing &      │
│  Security        AM Mode         Multi-Channel   Infrastructure │
│  ─────────       ─────────       ─────────       ─────────      │
│  Member 1        Member 3        Member 5        Member 7       │
│  Member 2        Member 4        Member 6        Member 8       │
│                                                                 │
│  Branch:         Branch:         Branch:         Branch:        │
│  feat/pdcp       feat/rlc        feat/mac        feat/testing   │
└─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                   MERGE DAY (Day 4)                             │
│  All 4 branches merge into main. Fix integration issues.       │
│  Run full test suite. Pair D leads merge, everyone helps.      │
└─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│               COLLECTIVE PHASE (Days 4–5)                      │
│  Everyone contributes to: profiling runs, charts, report,      │
│  user manual. Each pair writes their own report section.        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Day 0 (Today — Meeting)

**Everyone does this before starting their tasks:**

- [ ] Clone the repo
- [ ] Build and run: `mkdir build && cd build && cmake .. && make && ./5g_layer2`
- [ ] Confirm all 23 tests pass
- [ ] Read `docs/architecture.md` to understand the full pipeline
- [ ] Read the source code of YOUR assigned layer (spend 30 min understanding V1)
- [ ] Create your Git branch: `git checkout -b feat/<your-layer>`
- [ ] Agree on merge deadline with the team

---

## Pair A — PDCP Security (Members 1 & 2)

**Branch:** `feat/pdcp`
**Goal:** Replace placeholder crypto with real algorithms, add header compression.

### Member 1 — Ciphering & Integrity

**Focus:** Replace the XOR cipher and CRC32 integrity with proper algorithms.

| Day | Task | Done |
|-----|------|------|
| 1 | Install OpenSSL dev package (`sudo apt install libssl-dev`). Update `CMakeLists.txt` to link `-lssl -lcrypto`. Verify project still builds. | ☐ |
| 1 | Implement AES-128-CTR ciphering in `pdcp.cpp`. The function signature stays the same (`apply_cipher`), but the internals use OpenSSL's `EVP_EncryptInit_ex` with AES-128-CTR. The keystream is generated from (cipher_key, COUNT, bearer_id, direction) as the IV. | ☐ |
| 2 | Implement HMAC-SHA256 for integrity in `pdcp.cpp`. Replace `compute_mac_i` internals. Use OpenSSL's `HMAC()` function. Truncate the output to 4 bytes (first 4 bytes of the 32-byte HMAC) to match the MAC-I field size. | ☐ |
| 2 | Run all existing PDCP tests — they must still pass (round-trip: TX then RX produces identical data). | ☐ |
| 3 | Add new unit tests in `test_pdcp.cpp`: test that ciphered output differs from plaintext, test that wrong key fails integrity check, test that toggling cipher/integrity on/off works. | ☐ |
| 3 | Profile: measure time per packet for old (XOR/CRC32) vs new (AES/HMAC) using `std::chrono`. Record results in a small table for the report. | ☐ |

**Key files to edit:** `src/pdcp.cpp`, `include/pdcp.h`, `CMakeLists.txt`, `tests/test_pdcp.cpp`

**How to test independently:** Build and run `./test_pdcp` — does not depend on RLC or MAC at all.

---

### Member 2 — Header Compression

**Focus:** Add optional ROHC-style header compression to reduce IP header overhead.

| Day | Task | Done |
|-----|------|------|
| 1 | Study the V1 PDCP TX/RX flow in `pdcp.cpp`. Understand where compression fits: after receiving the SDU from upper layer, before ciphering (TX), and after deciphering, before delivering to upper layer (RX). | ☐ |
| 1 | Implement a simplified header compression: since all our IP packets use the same src/dst IP and protocol, compress by replacing the 20-byte IPv4 header with a 4-byte compressed header: `[0xFC marker (1 byte)] [packet ID (2 bytes)] [total length (1 byte delta from previous)]`. Decompress by restoring the full 20-byte header. | ☐ |
| 2 | Add a `compression_enabled` flag to `Config` (already exists, just wire it up). When enabled, compress before cipher on TX and decompress after decipher on RX. When disabled, pass through unchanged. | ☐ |
| 2 | Write unit tests: compressed round-trip matches original, compression actually reduces size, disabled compression passes data unchanged. | ☐ |
| 3 | Profile: measure PDCP processing time with and without compression. Measure byte reduction (original IP header size vs compressed header size). | ☐ |
| 3 | Write a short description (half a page) of the compression scheme for the report. Cite if you used any reference for the approach. | ☐ |

**Key files to edit:** `src/pdcp.cpp`, `include/pdcp.h`, `tests/test_pdcp.cpp`

**Coordination with Member 1:** You both edit `pdcp.cpp`. Agree on which functions each person touches. Member 1 works inside `apply_cipher` and `compute_mac_i`. Member 2 adds new functions `compress_header` and `decompress_header` and calls them in `process_tx` / `process_rx`. Communicate daily, merge your changes within the pair branch frequently.

---

## Pair B — RLC Acknowledged Mode (Members 3 & 4)

**Branch:** `feat/rlc`
**Goal:** Add AM mode with retransmission alongside the existing UM mode. This is the most complex task.

### Member 3 — AM Mode TX Side + STATUS PDU

**Focus:** Build the transmitting side of AM mode and the STATUS PDU format.

| Day | Task | Done |
|-----|------|------|
| 1 | Add a `RlcAmTx` class (or extend `RlcLayer` with AM mode). When `rlc_mode == 2` (AM), use the AM path. Keep UM mode untouched. | ☐ |
| 1 | Implement AM TX: assign 12-bit SN to each SDU, segment like UM but use AMD PDU format (D/C=1, P bit, SI, SN, SO fields). Store transmitted PDUs in a retransmission buffer indexed by SN. | ☐ |
| 2 | Define the STATUS PDU format: D/C=0, CPT=000, ACK_SN (12 bits), E1 bit, and optionally NACK_SN entries. Implement `parse_status_pdu()` to read a STATUS PDU and mark acknowledged SNs. Implement `build_status_pdu()` for the RX side (Member 4 will call this). | ☐ |
| 2 | Implement retransmission: when a STATUS PDU with NACK_SN is received, re-queue the corresponding PDU (or re-segment if needed) from the retransmission buffer. | ☐ |
| 3 | Write unit tests: AM TX produces correct headers, retransmission buffer stores PDUs correctly, STATUS PDU with NACKs triggers retransmission. | ☐ |

**Key files to edit:** `src/rlc.cpp`, `include/rlc.h`, `tests/test_rlc.cpp`

---

### Member 4 — AM Mode RX Side + Simulated Loss

**Focus:** Build the receiving side of AM mode, reassembly with gaps, and simulate packet loss.

| Day | Task | Done |
|-----|------|------|
| 1 | Implement AM RX: receive AMD PDUs, buffer them by SN, reassemble complete SDUs. Track `RX_Next` (next expected SN) and `RX_Highest_Status` (oldest missing SN). | ☐ |
| 1 | Implement in-order delivery: only deliver reassembled SDUs to upper layer in SN order. Hold back later SDUs until gaps are filled by retransmissions. | ☐ |
| 2 | Implement STATUS PDU generation on the RX side: when triggered (e.g., after detecting a gap), build a STATUS PDU listing NACK_SN for missing PDUs, send it back to TX side. | ☐ |
| 2 | Add a simulated loss mechanism in `main.cpp` or a test helper: randomly drop 1 in N RLC PDUs (configurable, e.g., `--loss-rate 0.1`). Show that AM mode recovers through retransmission while UM mode loses data. | ☐ |
| 3 | Write unit tests: reassembly with gaps, STATUS PDU triggers correct retransmissions, 100% recovery after simulated loss in AM mode, UM mode correctly loses data with same loss pattern. | ☐ |
| 3 | Profile: measure round-trip time with and without loss, count retransmissions needed. | ☐ |

**Key files to edit:** `src/rlc.cpp`, `include/rlc.h`, `tests/test_rlc.cpp`, `src/main.cpp` (loss simulation flag)

**Coordination between Members 3 & 4:** Member 3 builds the TX side and the STATUS PDU structure. Member 4 builds the RX side and generates STATUS PDUs. You need to agree on the STATUS PDU struct/format on Day 1. Work in the same branch, talk constantly. Test the full AM loop together by end of Day 2.

---

## Pair C — MAC Multi-Channel & Control (Members 5 & 6)

**Branch:** `feat/mac`
**Goal:** Add multi-channel multiplexing, prioritization, and MAC control elements.

### Member 5 — Multi-LCID Multiplexing & Logical Channel Prioritization

**Focus:** Support multiple logical channels and prioritize between them.

| Day | Task | Done |
|-----|------|------|
| 1 | Modify `MacLayer::process_tx` to accept SDUs from multiple logical channels. Change the input from `vector<ByteBuffer>` to something like `map<uint8_t, vector<ByteBuffer>>` where the key is LCID. Each SDU gets a subheader with its own LCID. | ☐ |
| 1 | Modify `MacLayer::process_rx` to demultiplex by LCID — return SDUs grouped by their logical channel. | ☐ |
| 2 | Implement basic Logical Channel Prioritization (LCP): each LCID has a priority and a Prioritized Bit Rate (PBR). When building a MAC PDU, serve higher-priority channels first up to their PBR, then fill remaining space round-robin. Define a simple `LcConfig` struct with `{lcid, priority, pbr_bytes}`. | ☐ |
| 2 | Test: create 2 logical channels with different priorities, feed SDUs to both, verify the higher-priority channel's SDUs appear first in the MAC PDU and get more space. | ☐ |
| 3 | Write unit tests for multi-LCID mux/demux and LCP ordering. Profile multiplexing overhead for 1 vs 2 vs 4 channels. | ☐ |

**Key files to edit:** `src/mac.cpp`, `include/mac.h`, `include/common.h` (add LcConfig), `tests/test_mac.cpp`

---

### Member 6 — MAC Control Elements & Variable TB Size

**Focus:** Add BSR (Buffer Status Report) MAC CE and support variable transport block sizes.

| Day | Task | Done |
|-----|------|------|
| 1 | Implement Short BSR MAC CE: LCID = 61 (for UL-SCH), 1-byte payload containing `[LCG ID (3 bits)] [Buffer Size Index (5 bits)]`. On TX side, the MAC layer calculates how much data is buffered across logical channels and includes a BSR in the MAC PDU before the SDUs. | ☐ |
| 1 | On the RX side, parse the BSR MAC CE by recognizing LCID 61 and extracting the buffer size. For V1, just log it — the BSR doesn't drive scheduling in our simulation, but it demonstrates the concept. | ☐ |
| 2 | Implement variable transport block size: instead of a fixed TB size, accept TB size as a per-call parameter to `process_tx`. If the SDUs + subheaders + BSR don't fill the TB, pad as before. If they exceed the TB, truncate (don't include SDUs that won't fit, signal back to upper layer). | ☐ |
| 2 | Add a command-line option `--variable-tb` that cycles through TB sizes (e.g., 512, 1024, 2048) across packets to simulate dynamic scheduling. | ☐ |
| 3 | Write unit tests: BSR present in MAC PDU, BSR parsed correctly on RX, variable TB sizes work correctly, truncation when TB is too small. | ☐ |
| 3 | Profile: measure MAC processing time vs TB size. | ☐ |

**Key files to edit:** `src/mac.cpp`, `include/mac.h`, `src/main.cpp` (variable TB flag), `tests/test_mac.cpp`

**Coordination between Members 5 & 6:** Both edit `mac.cpp`. Member 5 changes the mux/demux logic and the function signatures. Member 6 adds BSR insertion into the PDU and variable TB support. Agree on Day 1: Member 5 changes the interface first (multi-LCID input/output), Member 6 adapts to the new interface when adding BSR. Merge within the pair branch at end of each day.

---

## Pair D — Test Infrastructure & IP Generator (Members 7 & 8)

**Branch:** `feat/testing`
**Goal:** Enhance testing, improve the IP generator, prepare profiling infrastructure so the collective phase goes fast.

### Member 7 — Enhanced IP Generator & End-to-End Tests

**Focus:** Make the IP generator more realistic and write comprehensive integration tests.

| Day | Task | Done |
|-----|------|------|
| 1 | Enhance `IpGenerator`: add support for variable-size packets within a single run (e.g., `--packet-sizes 100,500,1000,1400`). Add different payload patterns (random bytes, all-zeros, all-0xFF) to test edge cases. | ☐ |
| 1 | Add a UDP header (8 bytes) after the IP header for more realism: `[src port (2)] [dst port (2)] [length (2)] [checksum=0 (2)]`. Update `verify_packet` to handle this. | ☐ |
| 2 | Write stress integration tests: 100+ packets, mixed sizes within one run, verify every single byte round-trips correctly. Add a `--stress` flag to `main.cpp`. | ☐ |
| 2 | Write edge case tests: minimum size packet (just headers, no payload), maximum size packet (9000 bytes — PDCP SDU limit), packet size exactly equal to RLC max PDU size (boundary test), TB size smaller than a single RLC PDU + subheader (forces MAC to handle it). | ☐ |
| 3 | Test each other pair's branch: pull `feat/pdcp`, `feat/rlc`, `feat/mac` one by one and run the full integration test suite against each. Report any failures to the owning pair. | ☐ |

**Key files to edit:** `src/ip_generator.cpp`, `include/ip_generator.h`, `tests/test_integration.cpp`, `src/main.cpp`

---

### Member 8 — Profiling Infrastructure & Build System

**Focus:** Build the tooling so that when we reach the collective phase, generating charts and results is fast.

| Day | Task | Done |
|-----|------|------|
| 1 | Enhance the CSV output in `main.cpp`: add columns for packet size, TB size, and each layer's TX and RX time in microseconds. Make sure the CSV is clean and machine-readable. | ☐ |
| 1 | Write a Python script `scripts/generate_charts.py` that reads `profiling_results.csv` and produces charts: (a) bar chart of avg time per layer, (b) line chart of time vs TB size per layer, (c) line chart of time vs packet size per layer. Use matplotlib. Save as PNG files in `docs/charts/`. | ☐ |
| 2 | Write a shell script `scripts/run_profiling.sh` that runs the binary with all parameter combinations: TB sizes [256, 512, 1024, 2048, 4096, 8192], packet sizes [100, 500, 1000, 1400, 3000], and appends all results to one big CSV. | ☐ |
| 2 | Set up a `Makefile` or CMake target `make profile` that runs the profiling script and generates charts in one command. | ☐ |
| 3 | Same as Member 7 Day 3: test each pair's branch against integration tests. Help with merge issues. | ☐ |

**Key files to create/edit:** `src/main.cpp` (CSV format), `scripts/generate_charts.py`, `scripts/run_profiling.sh`, `CMakeLists.txt`

**Coordination between Members 7 & 8:** Both touch `main.cpp` — agree that Member 7 owns the IP generator and stress-test flags, Member 8 owns the CSV output and profiling flags. Both help validate other pairs' branches on Day 3.

---

## Collective Phase — Everyone (Days 4–5)

Once all branches are merged:

| Task | Who | Day |
|------|-----|-----|
| Merge all 4 branches into `main` | Pair D leads, everyone helps fix conflicts | 4 |
| Run full integration test suite on merged code | Members 7 & 8 | 4 |
| Run full profiling suite (`make profile`) | Member 8 runs it, everyone reviews | 4 |
| Write report Section 1: Introduction & Background | Members 7 & 8 | 4–5 |
| Write report Section 2: PDCP implementation & results | Members 1 & 2 | 4–5 |
| Write report Section 3: RLC implementation & results | Members 3 & 4 | 4–5 |
| Write report Section 4: MAC implementation & results | Members 5 & 6 | 4–5 |
| Write report Section 5: Profiling analysis & charts | Member 8 + anyone available | 5 |
| Write report Section 6: Conclusion + Group roles table | Decided by team | 5 |
| Write the User Manual (2–5 pages) | Members 7 & 8 (they know the build/test system best) | 5 |
| Final review: everyone reads the report, checks for errors | All 8 | 5 |
| Submit | All 8 | 5 |

---

## Git Workflow Rules

1. **Never push to `main` directly.** All work goes to your pair's branch.
2. **Pull from `main` daily** to stay up-to-date: `git pull origin main` then merge into your branch.
3. **Within your pair:** either use a shared branch or sub-branches. Just communicate.
4. **Commit messages:** use the format `[LAYER] Short description` — e.g., `[PDCP] Replace XOR cipher with AES-128-CTR`, `[RLC] Add AM TX retransmission buffer`.
5. **Before merging to main (Day 4):** all 23 original tests MUST still pass. Your new tests must also pass.
6. **If you break the interface** (change function signatures in `.h` files): notify all pairs on the group chat immediately.

---

## Communication Checklist

- [ ] Group chat created (WhatsApp / Telegram / Discord)
- [ ] Daily 10-min standup agreed (time: ______)
- [ ] Each pair confirmed their task understanding
- [ ] Git repo URL shared with everyone
- [ ] Everyone built and ran V1 successfully
- [ ] Merge deadline agreed: Day ___ at ___:00
