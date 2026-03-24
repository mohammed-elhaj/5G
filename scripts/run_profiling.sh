#!/bin/bash
# ============================================================
# run_profiling.sh — Automated Profiling Suite
#
# Runs the 5G Layer 2 simulator with multiple combinations of:
#   - TB sizes: 256, 512, 1024, 2048, 4096, 8192 bytes
#   - Packet sizes: 100, 500, 1000, 1400, 3000 bytes
#
# All results are appended to profiling_results.csv
# ============================================================

set -e  # Exit on error

# Configuration
BINARY="./5g_layer2"
OUTPUT_CSV="profiling_results.csv"
NUM_PACKETS=10  # Number of packets per configuration

# Parameter arrays
TB_SIZES=(256 512 1024 2048 4096 8192)
PACKET_SIZES=(100 500 1000 1400 3000)

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "  5G Layer 2 Profiling Suite"
echo "========================================"
echo ""

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary $BINARY not found!"
    echo "Please build the project first:"
    echo "  mkdir -p build && cd build"
    echo "  cmake .. && make"
    exit 1
fi

# Remove old CSV if it exists
if [ -f "$OUTPUT_CSV" ]; then
    echo -e "${YELLOW}Removing old $OUTPUT_CSV${NC}"
    rm "$OUTPUT_CSV"
fi

# Calculate total runs
TOTAL_RUNS=$((${#TB_SIZES[@]} * ${#PACKET_SIZES[@]}))
CURRENT_RUN=0

echo -e "${BLUE}Running $TOTAL_RUNS profiling configurations...${NC}"
echo ""

# Create temporary directory for individual run CSVs
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Run all combinations
for TB_SIZE in "${TB_SIZES[@]}"; do
    for PACKET_SIZE in "${PACKET_SIZES[@]}"; do
        CURRENT_RUN=$((CURRENT_RUN + 1))
        
        echo -e "${GREEN}[$CURRENT_RUN/$TOTAL_RUNS]${NC} TB=$TB_SIZE, Packet=$PACKET_SIZE"
        
        # Run the simulator
        TEMP_CSV="$TEMP_DIR/run_${CURRENT_RUN}.csv"
        $BINARY --tb-size $TB_SIZE --packet-size $PACKET_SIZE --num-packets $NUM_PACKETS > /dev/null
        
        # Move the generated CSV to temp location
        if [ -f "$OUTPUT_CSV" ]; then
            mv "$OUTPUT_CSV" "$TEMP_CSV"
        else
            echo "WARNING: No CSV generated for this run"
            continue
        fi
    done
done

echo ""
echo -e "${BLUE}Merging results...${NC}"

# Merge all CSVs (keep header from first file only)
FIRST=true
for CSV_FILE in "$TEMP_DIR"/run_*.csv; do
    if [ "$FIRST" = true ]; then
        cat "$CSV_FILE" >> "$OUTPUT_CSV"
        FIRST=false
    else
        # Skip header line for subsequent files
        tail -n +2 "$CSV_FILE" >> "$OUTPUT_CSV"
    fi
done

# Count total records
TOTAL_RECORDS=$(tail -n +2 "$OUTPUT_CSV" | wc -l)

echo ""
echo "========================================"
echo -e "${GREEN}Profiling Complete!${NC}"
echo "========================================"
echo "  Total configurations: $TOTAL_RUNS"
echo "  Total packets tested: $TOTAL_RECORDS"
echo "  Output file: $OUTPUT_CSV"
echo ""
echo "Next steps:"
echo "  1. Generate charts: python scripts/generate_charts.py"
echo "  2. Or use: make profile (to run profiling + generate charts)"
echo ""
