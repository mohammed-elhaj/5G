#!/bin/bash
# ============================================================
# quick_test.sh — Quick Profiling Test
#
# Runs a minimal profiling test (2 configs) to verify
# the infrastructure works before running the full suite.
# ============================================================

set -e

BINARY="./5g_layer2"
OUTPUT_CSV="profiling_results.csv"

echo "========================================"
echo "  Quick Profiling Test"
echo "========================================"
echo ""

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary $BINARY not found!"
    echo "Build the project first: cmake .. && make"
    exit 1
fi

# Remove old CSV
[ -f "$OUTPUT_CSV" ] && rm "$OUTPUT_CSV"

echo "Running 2 test configurations..."
echo ""

# Test 1: Small packet, small TB
echo "[1/2] TB=512, Packet=500"
$BINARY --tb-size 512 --packet-size 500 --num-packets 5 > /dev/null
mv "$OUTPUT_CSV" temp1.csv

# Test 2: Large packet, large TB
echo "[2/2] TB=2048, Packet=1400"
$BINARY --tb-size 2048 --packet-size 1400 --num-packets 5 > /dev/null
mv "$OUTPUT_CSV" temp2.csv

# Merge
cat temp1.csv > "$OUTPUT_CSV"
tail -n +2 temp2.csv >> "$OUTPUT_CSV"
rm temp1.csv temp2.csv

echo ""
echo "✓ Test complete! Generated $OUTPUT_CSV with 10 records"
echo ""
echo "Next: python scripts/generate_charts.py"
echo ""
