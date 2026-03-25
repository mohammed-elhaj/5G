# The MAC Layer

## Role in the Architecture

The Medium Access Control (MAC) layer sits between the RLC layer and the physical layer in the 5G NR Layer 2 stack. Its job is to take data from one or more logical channels above it, pack that data into a single Transport Block for transmission over the radio interface, and reverse the process on the receiving side. In 3GPP TS 38.321 (§4.4), the MAC layer is assigned three core functions: multiplexing and demultiplexing data from multiple logical channels into a shared Transport Block (§6.1.2), deciding which channels get served first and how much airtime each receives through Logical Channel Prioritization (§5.4.3.1), and generating control information such as Buffer Status Reports that tell the network how much data the device has waiting to send (§6.1.3).

## V1 — Baseline

The original MAC implementation handled the basic case: it took data from a single logical channel, added subheaders containing the channel ID and length, packed everything into a fixed 2048-byte Transport Block, and filled the remaining space with padding. On the receive side, it parsed the subheaders and extracted the original data. The end-to-end loopback confirmed that every IP packet survived the full uplink-downlink round trip byte-for-byte.

V1 had three limitations. First, it only supported one logical channel, meaning it could not mix different types of traffic (such as voice and data) in the same Transport Block. Second, there was no scheduling — data was packed in whatever order it arrived, with no way to prioritise time-sensitive traffic. Third, the entire Transport Block was zero-filled on every transmission, even when most of it was already occupied by data, wasting processing effort.

---

# Pair C Changes

## V2 — Multi-Channel Support (Member 5)

V2 added the ability to accept data from multiple logical channels and pack them into the same Transport Block, each tagged with its own channel identity. On the receive side, the demultiplexer now returns each piece of data with its channel tag preserved, so the layer above can route it to the correct destination. The original single-channel interface was kept as a wrapper that internally uses the new multi-channel path, so all existing code continues to work without changes.

V2 also fixed the zero-fill issue: instead of clearing the entire Transport Block before use, only the padding region at the end is now zeroed after all data has been packed.

## V3 — Channel Scheduling (Member 5)

V3 added Logical Channel Prioritization following the procedure defined in TS 38.321 §5.4.3.1. When enabled, channels are served in priority order. Each channel first receives up to its guaranteed byte quota (the Prioritized Bit Rate). Any remaining space in the Transport Block is then distributed across channels in a round-robin cycle, preventing lower-priority channels from being starved entirely. When scheduling is disabled, the behaviour is identical to V2.

Minor efficiency improvements were also made: pre-allocating memory for output buffers, caching loop variables, and reordering the receive-side parsing to check for padding (the most common case) before checking for control elements (the rare case).

## V3 + Member 6 — Buffer Status Report and Variable TB Size

Member 6 added the ability to insert a Buffer Status Report (BSR) at the front of the Transport Block, following TS 38.321 §6.1.3.1. The BSR is a 2-byte control message that tells the network how much uplink data the device has queued, so the network can allocate a larger transmission grant in the next scheduling interval. The BSR is only inserted when the feature is enabled via a configuration flag.

Member 6 also added the ability to override the Transport Block size on a per-call basis, and graceful handling for cases where the Transport Block is too small to fit the available data — the function stops cleanly rather than overflowing the buffer.

During integration, corrections were needed: the BSR was initially inserted unconditionally regardless of the configuration flag, and the scheduling logic had been simplified in a way that removed the priority ordering. Both issues were resolved, after which all tests passed.

---

# Test Results

The original 5 MAC tests continue to pass with default configuration, confirming that V1 behaviour is fully preserved. Pair C added 7 new tests targeting the features introduced in V2 and V3. All end-to-end integration tests also remain unaffected (reported in the Integration Testing section of this report).

| Test | Verifies | Result |
|------|----------|--------|
| 5 original MAC tests | Single-channel mux/demux, subheaders, and padding under default config | PASS |
| Multi-LCID round-trip | 2 channels with 2 data units each; correct channel tags after demux | PASS |
| LCP priority ordering | Higher-priority channel appears first in the Transport Block | PASS |
| LCP byte quota | Each channel's guaranteed quota is respected; remainder via round-robin | PASS |
| LCP 3-channel round-robin | 3 channels cycle correctly (4→5→6→4→...) after the quota phase | PASS |
| BSR present in TB | BSR control element appears at the start of the TB when enabled | PASS |
| Variable TB size | Overriding TB size produces correctly sized output | PASS |
| Truncation | Undersized TB produces valid output with padding only, no crash | PASS |
| **MAC total** | **5 original + 7 new** | **12 / 12** |

---

# Performance

*All measurements below were taken on WSL2 with 10,000 iterations per variant per packet size. WSL2 introduces timer jitter of ±0.3–1.5µs; absolute values are indicative only. Pair D will produce definitive measurements on native Linux.*

| Packet Size | Variant | TX avg (µs) | RX avg (µs) | Overhead | Correctness |
|-------------|---------|-------------|-------------|----------|-------------|
| 100 B | V1-Baseline | ~0.79 | ~0.15 | 2–3 B | PASS |
| 100 B | Multi-LCID | ~0.55 | ~0.13 | 4 B | PASS |
| 100 B | LCP-On | ~0.62 | ~0.18 | 4 B | PASS |
| 100 B | BSR-On | ~0.80 | ~0.14 | 4 B | PASS |
| 500 B | V1-Baseline | ~0.75 | ~0.16 | 3 B | PASS |
| 500 B | Multi-LCID | ~0.60 | ~0.17 | 4 B | PASS |
| 500 B | LCP-On | ~0.65 | ~0.20 | 4 B | PASS |
| 500 B | BSR-On | ~0.65 | ~0.14 | 5 B | PASS |
| 1000 B | V1-Baseline | ~0.70 | ~0.14 | 3 B | PASS |
| 1000 B | Multi-LCID | ~0.68 | ~0.18 | 6 B | PASS |
| 1000 B | LCP-On | ~0.75 | ~0.22 | 6 B | PASS |
| 1000 B | BSR-On | ~0.70 | ~0.13 | 5 B | PASS |
| 1400 B | V1-Baseline | ~0.72 | ~0.17 | 3 B | PASS |
| 1400 B | Multi-LCID | ~0.62 | ~0.19 | 6 B | PASS |
| 1400 B | LCP-On | ~0.65 | ~0.24 | 6 B | PASS |
| 1400 B | BSR-On | ~1.20 | ~0.25 | 5 B | PASS |
| 3000 B | V1-Baseline | ~0.90 | ~0.26 | 3 B | PASS |
| 3000 B | Multi-LCID | ~0.85 | ~0.33 | 6 B | PASS |
| 3000 B | LCP-On | ~0.85 | ~0.35 | 6 B | PASS |
| 3000 B | BSR-On | ~1.20 | ~0.23 | 5 B | PASS |

## Analysis

TX latencies across all four variants remain in the sub-microsecond range, confirming that the new features do not degrade throughput. The zero-fill fix is the only measurable latency improvement, saving approximately 600 bytes of unnecessary writes per call at the 1400B packet size. Subheader overhead increases from 2–3 bytes to 4–6 bytes with multiple channels, which is the expected cost of the 3GPP subheader format (one subheader per data unit per channel). TB efficiency stands at 68.4% for a 1400B packet in a 2048B TB; this is a function of the fixed TB size rather than the MAC layer's packing logic.

*The MAC layer's primary contribution is standards-compliant functionality — multi-channel multiplexing, priority-based scheduling, and buffer status reporting — rather than raw speed improvement.*

---

# References

[1] 3GPP TS 38.321 V17.5.0, "Medium Access Control (MAC) protocol specification," §4.4: MAC layer functions.

[2] 3GPP TS 38.321, §5.4.3.1: Logical Channel Prioritization procedure.

[3] 3GPP TS 38.321, §6.1.2: MAC PDU format (multiplexing and subheader structure).

[4] 3GPP TS 38.321, §6.1.3.1: Buffer Status Report MAC Control Element.

[5] 3GPP TS 38.300 V17.5.0, "NR; NR and NG-RAN Overall Description," §4.4.2: Layer 2 overview.
