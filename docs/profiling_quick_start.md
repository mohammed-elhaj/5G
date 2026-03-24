# Profiling Quick Start Guide

This guide shows how to use the automated profiling infrastructure built by Member 8 (Pair D).

## Prerequisites

1. Python 3.x installed
2. Install Python dependencies:
   ```bash
   pip install -r scripts/requirements.txt
   ```

## Quick Start — One Command

From the `build` directory, run:

```bash
make profile
```

This will:
- ✓ Run 30 profiling configurations (300 packets total)
- ✓ Generate 3 publication-quality charts
- ✓ Save everything to `docs/charts/`

**Time estimate:** 2-5 minutes depending on your machine

## Step-by-Step (Manual)

If you prefer to run steps individually:

### 1. Build the Project
```bash
mkdir -p build && cd build
cmake ..
make
```

### 2. Run Profiling
**Linux/Mac:**
```bash
bash ../scripts/run_profiling.sh
```

**Windows:**
```cmd
..\scripts\run_profiling.bat
```

### 3. Generate Charts
```bash
python ../scripts/generate_charts.py
```

## Output Files

After profiling, you'll have:

```
build/
  └── profiling_results.csv          # Raw profiling data (300 rows)

docs/charts/
  ├── chart1_avg_time_per_layer.png  # Bar chart: avg time per layer
  ├── chart2_time_vs_tb_size.png     # Line charts: time vs TB size
  └── chart3_time_vs_packet_size.png # Line charts: time vs packet size
```

## Understanding the CSV Format

The `profiling_results.csv` contains these columns:

| Column | Description |
|--------|-------------|
| `seq` | Packet sequence number |
| `packet_size` | IP packet size in bytes |
| `tb_size` | Transport block size in bytes |
| `pdcp_tx_us` | PDCP TX processing time (microseconds) |
| `rlc_tx_us` | RLC TX processing time (microseconds) |
| `mac_tx_us` | MAC TX processing time (microseconds) |
| `mac_rx_us` | MAC RX processing time (microseconds) |
| `rlc_rx_us` | RLC RX processing time (microseconds) |
| `pdcp_rx_us` | PDCP RX processing time (microseconds) |
| `total_tx_us` | Total uplink time (sum of TX) |
| `total_rx_us` | Total downlink time (sum of RX) |
| `total_us` | Complete round-trip time |
| `pass` | Verification result (PASS/FAIL) |

## Customizing Profiling Parameters

Edit the scripts to test different configurations:

**In `run_profiling.sh` or `run_profiling.bat`:**
```bash
TB_SIZES=(256 512 1024 2048 4096 8192)      # Modify these arrays
PACKET_SIZES=(100 500 1000 1400 3000)
NUM_PACKETS=10                               # Packets per config
```

**For a quick test (fewer configurations):**
```bash
TB_SIZES=(512 2048)
PACKET_SIZES=(500 1400)
NUM_PACKETS=5
```

## Troubleshooting

**"Binary not found" error:**
- Make sure you're in the `build` directory
- Run `make` to build the project first

**"Module not found" error (Python):**
- Install dependencies: `pip install -r scripts/requirements.txt`

**Charts look wrong:**
- Verify `profiling_results.csv` has the correct format (13 columns)
- If using old CSV format, rebuild the project with updated `main.cpp`

**Permission denied (Linux/Mac):**
```bash
chmod +x scripts/run_profiling.sh
```

## For the Report

The generated charts are high-resolution (300 DPI) and ready for inclusion in your report. Each chart clearly shows:

1. **Chart 1:** Which layer is the bottleneck (TX vs RX)
2. **Chart 2:** How layers scale with transport block size
3. **Chart 3:** How layers scale with packet size

Include the summary statistics printed by `generate_charts.py` in your analysis section.

## CMake Targets Reference

| Target | Command | Description |
|--------|---------|-------------|
| `profile` | `make profile` | Complete workflow (profiling + charts) |
| `run_profiling` | `make run_profiling` | Run profiling only |
| `generate_charts` | `make generate_charts` | Generate charts from existing CSV |
| `clean_profiling` | `make clean_profiling` | Remove profiling data and charts |

## Questions?

Contact Member 8 (Testing & Infrastructure) or check `scripts/README.md` for more details.
