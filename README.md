# 5G NR Layer 2 Protocol Stack Simulator

A simplified 5G NR Layer 2 protocol stack simulator (UE-side) written in **C++17**. The system processes dummy IP packets through the full uplink chain (PDCP -> RLC -> MAC), produces a Transport Block, then loops it back through the downlink chain (MAC -> RLC -> PDCP) to recover the original IP packets and verify byte-for-byte correctness.

## Architecture

```
UPLINK (Transmit):
  IP Packet Generator
        |
        v
  +-------------+
  |    PDCP      |  -> Add SN header, cipher (XOR), compute integrity (CRC32), append MAC-I
  |  (TX side)   |  -> Output: PDCP PDU = [Header | Ciphered Payload | MAC-I]
  +------+------+
        |  PDCP PDU becomes RLC SDU
        v
  +-------------+
  |    RLC       |  -> Segment if SDU > grant size, add RLC header (SI, SN, SO)
  |  (TX side)   |  -> Output: one or more RLC PDUs
  +------+------+
        |  Each RLC PDU becomes a MAC SDU
        v
  +-------------+
  |    MAC       |  -> Multiplex MAC SDUs, add subheaders (R/F/LCID/L), pad to TB size
  |  (TX side)   |  -> Output: MAC PDU = Transport Block
  +------+------+
        |
        v
  TRANSPORT BLOCK (loopback)
        |
        v
DOWNLINK (Receive):
  +-------------+
  |    MAC       |  -> Parse subheaders, demux MAC SDUs by LCID
  |  (RX side)   |  -> Output: MAC SDUs = RLC PDUs
  +------+------+
        |
        v
  +-------------+
  |    RLC       |  -> Reassemble RLC SDU from segments using SI/SN/SO
  |  (RX side)   |  -> Output: complete RLC SDU = PDCP PDU
  +------+------+
        |
        v
  +-------------+
  |    PDCP      |  -> Decipher, verify MAC-I, remove PDCP header
  |  (RX side)   |  -> Output: original IP packet
  +------+------+
        |
        v
  IP Packet Sink (verify matches original)
```

## Project Structure

```
5G/
├── CMakeLists.txt                      # Build configuration with OpenSSL support
├── .gitignore
├── README.md                           # This file
├── git_workflow_guide.md               # Git branching and PR workflow
├── team_tasks_final.md                 # Final task assignments and completion status
├── testing_and_profiling_guide.md      # Comprehensive testing guide
├── include/
│   ├── common.h                        # Shared types: ByteBuffer, Config, LcData, LayerProfile
│   ├── ip_generator.h                  # Dummy IPv4 packet generator / verification sink
│   ├── pdcp.h                          # PDCP layer interface (TS 38.323)
│   ├── rlc.h                           # RLC layer interface, UM/AM modes (TS 38.322)
│   └── mac.h                           # MAC layer interface with multi-LCID support (TS 38.321)
├── src/
│   ├── main.cpp                        # Entry point: loopback pipeline + CSV profiling output
│   ├── ip_generator.cpp                # Generates fake IP packets with verifiable payload patterns
│   ├── pdcp.cpp                        # PDCP TX/RX: AES-128-CTR, HMAC-SHA256, header compression
│   ├── rlc.cpp                         # RLC TX/RX: UM/AM modes with optimizations
│   └── mac.cpp                         # MAC TX/RX: multi-LCID mux/demux, LCP scheduling, BSR
├── tests/
│   ├── test_pdcp.cpp                   # PDCP tests (23 tests: V1 + AES + HMAC + compression)
│   ├── test_rlc.cpp                    # RLC tests (17 tests: UM + AM + optimizations)
│   ├── test_mac.cpp                    # MAC tests (11 tests: V1 + multi-LCID + LCP + BSR)
│   └── test_integration.cpp            # Full pipeline end-to-end tests (7 tests)
├── scripts/
│   ├── README.md                       # Scripts documentation
│   ├── generate_charts.py              # Python script to generate profiling charts
│   ├── quick_test.sh / .bat            # Quick test runner for all test suites
│   ├── run_profiling.sh / .bat         # Automated profiling suite (30 configurations)
│   └── requirements.txt                # Python dependencies (pandas, matplotlib, numpy)
└── docs/
    ├── setup_guide.md                  # Step-by-step WSL2/Ubuntu setup with OpenSSL
    ├── architecture.md                 # Detailed data flow and PDU format documentation
    ├── team_tasks.md                   # Original task assignments
    ├── profiling_guide.md              # Manual profiling instructions
    ├── profiling_quick_start.md        # Automated profiling quick start
    ├── pair_a_pdcp_report.md           # Pair A: PDCP security & compression
    ├── member1_implementation_report.md # Member 1: AES-128-CTR & HMAC-SHA256
    ├── member2_compression_report.md   # Member 2: ROHC-style header compression
    ├── pair_b_rlc_report.md            # Pair B: RLC AM mode & optimizations
    ├── pair_c_mac_report.md            # Pair C: MAC multi-LCID, LCP, BSR
    ├── member5_mac_layer_report.md     # Member 5: MAC implementation details
    └── pair_d_testing_report.md        # Pair D: Testing infrastructure & profiling
```

## Quick Start

### Prerequisites

- Linux or WSL2 on Windows
- g++ with C++17 support (tested with g++ 13.3.0)
- CMake 3.14+
- make
- OpenSSL development libraries (libssl-dev)

### Build

```bash
# Install dependencies (Ubuntu/WSL)
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev

# Build the project
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run

```bash
# Run with default settings (1400-byte packets, 10 packets, 2048-byte TB)
./5g_layer2

# Run with custom parameters
./5g_layer2 --packet-size 500 --num-packets 50 --tb-size 4096
```

**Command-line options:**

| Option           | Default | Description                     |
|------------------|---------|---------------------------------|
| `--packet-size`  | 1400    | IP packet size in bytes         |
| `--num-packets`  | 10      | Number of packets to process    |
| `--tb-size`      | 2048    | Transport Block size in bytes   |

### Run Tests

```bash
cd build

# Run individual test suites
./test_pdcp           # 23 PDCP layer tests (V1 + AES + HMAC + compression)
./test_rlc            # 17 RLC layer tests (UM + AM modes + optimizations)
./test_mac            # 11 MAC layer tests (V1 + multi-LCID + LCP + BSR)
./test_integration    # 7 end-to-end tests

# Run all tests at once
./test_pdcp && ./test_rlc && ./test_mac && ./test_integration
```

**Total: 58 tests across all layers**

### Profiling Output

The simulator writes per-packet timing data to `profiling_results.csv` in the working directory. 

**Quick profiling:**
```bash
# Automated profiling suite (30 configurations, ~2-5 minutes)
bash ../scripts/run_profiling.sh

# Generate charts
python3 ../scripts/generate_charts.py
```

Charts are saved to `docs/charts/`. See [docs/profiling_quick_start.md](docs/profiling_quick_start.md) for details.

## Documentation

- **[Setup Guide](docs/setup_guide.md)** — Step-by-step instructions for setting up the development environment on WSL2/Ubuntu with OpenSSL
- **[Architecture](docs/architecture.md)** — Detailed explanation of the data flow, PDU formats, and design decisions
- **[Profiling Quick Start](docs/profiling_quick_start.md)** — Automated profiling and chart generation
- **[Profiling Guide](docs/profiling_guide.md)** — Manual profiling instructions and analysis techniques
- **[Git Workflow Guide](git_workflow_guide.md)** — Branching strategy and pull request workflow
- **[Testing Guide](testing_and_profiling_guide.md)** — Comprehensive testing documentation

### Team Implementation Reports

- **[Pair A: PDCP Report](docs/pair_a_pdcp_report.md)** — Security (AES-128-CTR, HMAC-SHA256) and header compression
  - [Member 1 Report](docs/member1_implementation_report.md) — AES-128-CTR and HMAC-SHA256 implementation
  - [Member 2 Report](docs/member2_compression_report.md) — ROHC-style IPv4 header compression
- **[Pair B: RLC Report](docs/pair_b_rlc_report.md)** — AM mode, retransmissions, and performance optimizations
- **[Pair C: MAC Report](docs/pair_c_mac_report.md)** — Multi-LCID multiplexing, LCP scheduling, and BSR
  - [Member 5 Report](docs/member5_mac_layer_report.md) — MAC layer implementation details
- **[Pair D: Testing Report](docs/pair_d_testing_report.md)** — Testing infrastructure and automated profiling

## Features

### PDCP Layer (Pair A)
- **V1 Baseline:** XOR stream cipher, CRC32 integrity
- **Member 1:** AES-128-CTR ciphering (NEA2), HMAC-SHA256 integrity (NIA2)
- **Member 2:** Simplified ROHC-style IPv4 header compression (13 bytes saved per packet)
- Config flags preserve V1 behavior by default

### RLC Layer (Pair B)
- **V1 Baseline:** UM mode with segmentation/reassembly
- **Member 3:** AM mode with retransmissions, polling, and status reporting
- **Member 4:** Loss simulation and performance optimizations (reserve, move semantics, fast-path reassembly)
- Config flags: `rlc_mode` (UM/AM), `rlc_opt_level`, `loss_rate`

### MAC Layer (Pair C)
- **V1 Baseline:** Single LCID multiplexing with padding
- **Member 5:** Multi-LCID multiplexing/demultiplexing, Logical Channel Prioritization (LCP) with PBR quotas
- **Member 6:** Buffer Status Report (BSR) MAC CE, variable TB size support
- **V3 Optimizations:** RX path micro-optimizations (reserve, branch reordering)
- Config flags: `num_logical_channels`, `lcp_enabled`, `bsr_enabled`

### Testing & Infrastructure (Pair D)
- **Member 8:** Automated profiling scripts, chart generation, CMake targets
- 58 total tests across all layers
- CSV profiling output with 13 columns per packet
- Publication-quality charts (300 DPI)

## Implementation Comparison

| Feature | V1 Baseline | Current Implementation |
|---------|-------------|------------------------|
| **PDCP ciphering** | XOR stream cipher | AES-128-CTR (NEA2) + XOR (configurable) |
| **PDCP integrity** | CRC32 | HMAC-SHA256 (NIA2) + CRC32 (configurable) |
| **PDCP compression** | None | Simplified ROHC for IPv4 (optional) |
| **RLC mode** | UM only | UM + AM with retransmissions |
| **RLC optimizations** | Baseline | reserve(), move semantics, fast-path reassembly |
| **MAC scheduling** | Single LCID, fixed grant | Multi-LCID with LCP scheduling |
| **MAC control elements** | None | BSR (Buffer Status Report) |
| **Logical channels** | Single LCID (4) | Up to 32 LCIDs with priority-based scheduling |
| **Test coverage** | 23 tests | 58 tests |
| **Profiling** | Manual CSV | Automated suite + chart generation |

## 3GPP References

| Spec | Layer | Key Sections |
|------|-------|-------------|
| TS 38.323 | PDCP | 5.6 (compression), 5.7 (ciphering), 5.8 (integrity), 6.2 (PDU formats) |
| TS 38.322 | RLC | 4.2.1.2 (UM model), 4.2.1.3 (AM model), 6.2.2.3 (UMD PDU), 6.2.1.4 (AMD PDU) |
| TS 38.321 | MAC | 6.1.2 (MAC PDU), 6.2.1 (subheader), 5.4.3.1 (LCP), 6.1.3.1 (BSR) |
| TS 38.300 | Overall | 4.4.2 (Layer 2 overview) |

## Team Contributions

This project was developed by 8 team members organized into 4 pairs:

- **Pair A (PDCP):** Member 1 (Security), Member 2 (Compression)
- **Pair B (RLC):** Member 3 (AM Mode), Member 4 (Optimizations)
- **Pair C (MAC):** Member 5 (Multi-LCID/LCP), Member 6 (BSR/Variable TB)
- **Pair D (Testing):** Member 7, Member 8 (Infrastructure & Profiling)

All features are backward-compatible with V1 through configuration flags.
