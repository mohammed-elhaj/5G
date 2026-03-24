# Profiling Scripts

This directory contains scripts for automated profiling and chart generation.

## Setup

Install Python dependencies:

```bash
pip install -r requirements.txt
```

Or if using pip3:

```bash
pip3 install -r requirements.txt
```

## Usage

### Generate Charts from Existing CSV

After running the simulator and generating `profiling_results.csv`, create charts:

```bash
python scripts/generate_charts.py
```

Or:

```bash
python3 scripts/generate_charts.py
```

This will:
- Read `profiling_results.csv` from the current directory
- Generate 3 charts in `docs/charts/`:
  1. `chart1_avg_time_per_layer.png` - Bar chart of average time per layer
  2. `chart2_time_vs_tb_size.png` - Line charts showing time vs transport block size
  3. `chart3_time_vs_packet_size.png` - Line charts showing time vs packet size
- Print summary statistics to console

### Run Full Profiling Suite

The profiling scripts automate running the simulator with multiple parameter combinations.

**On Linux/Mac:**
```bash
bash scripts/run_profiling.sh
```

**On Windows:**
```cmd
scripts\run_profiling.bat
```

This will test 30 configurations (all combinations of):
- TB sizes: 256, 512, 1024, 2048, 4096, 8192 bytes
- Packet sizes: 100, 500, 1000, 1400, 3000 bytes
- 10 packets per configuration = 300 total packets

All results are merged into a single `profiling_results.csv` file.

### One-Command Profiling with CMake

The easiest way to run the complete profiling workflow:

```bash
cd build
cmake --build . --target profile
```

Or with make:

```bash
cd build
make profile
```

This single command will:
1. Build the simulator (if needed)
2. Run all 30 profiling configurations
3. Generate all 3 charts automatically
4. Save charts to `docs/charts/`

### Other CMake Targets

**Run profiling only (no charts):**
```bash
make run_profiling
```

**Generate charts only (from existing CSV):**
```bash
make generate_charts
```

**Clean profiling data:**
```bash
make clean_profiling
```

## Output

All charts are saved as high-resolution PNG files (300 DPI) in `docs/charts/` for inclusion in the final report.
