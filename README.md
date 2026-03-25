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
├── CMakeLists.txt              # Build configuration
├── .gitignore
├── README.md                   # This file
├── include/
│   ├── common.h                # Shared types: ByteBuffer, Config, LayerProfile
│   ├── ip_generator.h          # Dummy IPv4 packet generator / verification sink
│   ├── pdcp.h                  # PDCP layer interface (TS 38.323 simplified)
│   ├── rlc.h                   # RLC layer interface, UM mode (TS 38.322 simplified)
│   └── mac.h                   # MAC layer interface (TS 38.321 simplified)
├── src/
│   ├── main.cpp                # Entry point: loopback pipeline + per-layer profiling
│   ├── ip_generator.cpp        # Generates fake IP packets with verifiable payload patterns
│   ├── pdcp.cpp                # PDCP TX/RX: XOR stream cipher + CRC32 integrity
│   ├── rlc.cpp                 # RLC TX/RX: UM segmentation (SI/SN/SO) and reassembly
│   └── mac.cpp                 # MAC TX/RX: subheader multiplexing + padding (LCID 63)
├── tests/
│   ├── test_pdcp.cpp           # PDCP round-trip tests (6 tests)
│   ├── test_rlc.cpp            # RLC segmentation/reassembly tests (5 tests)
│   ├── test_mac.cpp            # MAC mux/demux tests (5 tests)
│   └── test_integration.cpp    # Full pipeline end-to-end tests (7 tests)
└── docs/
    ├── setup_guide.md          # Step-by-step WSL2/Ubuntu setup instructions
    ├── architecture.md         # Detailed data flow and PDU format documentation
    ├── team_tasks.md           # Post-V1 optimization task assignments
    └── profiling_guide.md      # How to run profiling and generate charts
```

## Quick Start

### Prerequisites

- Linux or WSL2 on Windows
- g++ with C++17 support (tested with g++ 13.3.0)
- CMake 3.14+
- make

### Build

```bash
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
./test_pdcp           # 6 PDCP layer tests
./test_rlc            # 5 RLC layer tests
./test_mac            # 5 MAC layer tests
./test_integration    # 7 end-to-end tests

# Run all tests at once
./test_pdcp && ./test_rlc && ./test_mac && ./test_integration
```

### Profiling Output

The simulator writes per-packet timing data to `profiling_results.csv` in the working directory. See [docs/profiling_guide.md](docs/profiling_guide.md) for analysis instructions.

## Documentation

- **[Setup Guide](docs/setup_guide.md)** — Step-by-step instructions for setting up the development environment on WSL2/Ubuntu
- **[Architecture](docs/architecture.md)** — Detailed explanation of the data flow, PDU formats, and design simplifications
- **[Team Tasks](docs/team_tasks.md)** — Post-V1 optimization task assignments for team members
- **[Profiling Guide](docs/profiling_guide.md)** — How to run profiling sweeps and generate performance charts

## V1 Simplifications

| Feature | Full 3GPP Spec | Our V1 |
|---------|---------------|--------|
| PDCP ciphering | NEA1/NEA2/NEA3 | XOR-based stream cipher |
| PDCP integrity | NIA1/NIA2/NIA3 | CRC32-based MAC-I |
| RLC mode | TM / UM / AM | UM only (no retransmission) |
| MAC scheduling | Dynamic grants, LCP | Fixed grant size |
| MAC CEs | BSR, PHR, DRX | None (SDUs + padding only) |
| Logical channels | Up to 32 | Single LCID (4) |
| HARQ | Soft combining | Not implemented |

## 3GPP References

| Spec | Layer | Key Sections |
|------|-------|-------------|
| TS 38.323 | PDCP | 5.7 (ciphering), 5.8 (integrity), 6.2 (PDU formats) |
| TS 38.322 | RLC | 4.2.1.2 (UM model), 6.2.2.3 (UMD PDU), 5.2.2 (UM transfer) |
| TS 38.321 | MAC | 6.1.2 (MAC PDU), 6.2.1 (subheader), 5.4.3.1 (LCP) |
| TS 38.300 | Overall | 4.4.2 (Layer 2 overview) |
# 5G_v1
# 5G_v1
