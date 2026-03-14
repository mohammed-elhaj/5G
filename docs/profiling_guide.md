# Profiling Guide

How to run performance profiling on the 5G NR Layer 2 simulator and generate charts from the results.

---

## Running the Profiler

The main executable automatically records per-packet, per-layer timing and writes it to `profiling_results.csv` in the current working directory.

### Basic Run

```bash
cd build
./5g_layer2
```

### Sweep Across Transport Block Sizes

```bash
cd build
for tb in 256 512 1024 2048 4096 8192; do
    echo "=== TB size: $tb ==="
    ./5g_layer2 --tb-size $tb --num-packets 100
    mv profiling_results.csv profiling_tb_${tb}.csv
done
```

### Sweep Across IP Packet Sizes

```bash
cd build
for pkt in 100 500 1000 1400 2000; do
    echo "=== Packet size: $pkt ==="
    ./5g_layer2 --packet-size $pkt --num-packets 100
    mv profiling_results.csv profiling_pkt_${pkt}.csv
done
```

### High-Volume Stress Run

```bash
./5g_layer2 --num-packets 1000 --packet-size 1400 --tb-size 2048
```

---

## CSV Output Format

The file `profiling_results.csv` has the following columns:

| Column | Description |
|--------|-------------|
| `seq` | Packet sequence number (0-based) |
| `pdcp_tx_us` | PDCP TX processing time in microseconds |
| `rlc_tx_us` | RLC TX processing time in microseconds |
| `mac_tx_us` | MAC TX processing time in microseconds |
| `mac_rx_us` | MAC RX processing time in microseconds |
| `rlc_rx_us` | RLC RX processing time in microseconds |
| `pdcp_rx_us` | PDCP RX processing time in microseconds |
| `pass` | PASS or FAIL |

Example:
```csv
seq,pdcp_tx_us,rlc_tx_us,mac_tx_us,mac_rx_us,rlc_rx_us,pdcp_rx_us,pass
0,34.800,2.100,0.600,1.200,7.500,33.200,PASS
1,23.800,1.900,0.500,1.100,7.200,30.100,PASS
```

---

## Generating Charts

### Option 1: Python with matplotlib

Install matplotlib if needed:
```bash
pip install matplotlib pandas
```

**Bar chart — average time per layer:**
```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('profiling_results.csv')

layers = ['PDCP', 'RLC', 'MAC']
tx_avgs = [df['pdcp_tx_us'].mean(), df['rlc_tx_us'].mean(), df['mac_tx_us'].mean()]
rx_avgs = [df['pdcp_rx_us'].mean(), df['rlc_rx_us'].mean(), df['mac_rx_us'].mean()]

x = range(len(layers))
width = 0.35
fig, ax = plt.subplots()
ax.bar([i - width/2 for i in x], tx_avgs, width, label='Uplink (TX)')
ax.bar([i + width/2 for i in x], rx_avgs, width, label='Downlink (RX)')
ax.set_ylabel('Time (microseconds)')
ax.set_title('Average Processing Time per Layer')
ax.set_xticks(x)
ax.set_xticklabels(layers)
ax.legend()
plt.tight_layout()
plt.savefig('layer_timing.png', dpi=150)
plt.show()
```

**Line chart — per-packet total time:**
```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('profiling_results.csv')
df['total_ul'] = df['pdcp_tx_us'] + df['rlc_tx_us'] + df['mac_tx_us']
df['total_dl'] = df['mac_rx_us'] + df['rlc_rx_us'] + df['pdcp_rx_us']

plt.plot(df['seq'], df['total_ul'], label='Uplink')
plt.plot(df['seq'], df['total_dl'], label='Downlink')
plt.xlabel('Packet Sequence Number')
plt.ylabel('Total Time (microseconds)')
plt.title('Per-Packet Processing Time')
plt.legend()
plt.tight_layout()
plt.savefig('per_packet_timing.png', dpi=150)
plt.show()
```

**Compare across TB sizes** (after running the sweep):
```python
import pandas as pd
import matplotlib.pyplot as plt

tb_sizes = [256, 512, 1024, 2048, 4096, 8192]
avg_times = []

for tb in tb_sizes:
    df = pd.read_csv(f'profiling_tb_{tb}.csv')
    total = (df['pdcp_tx_us'] + df['rlc_tx_us'] + df['mac_tx_us'] +
             df['mac_rx_us'] + df['rlc_rx_us'] + df['pdcp_rx_us']).mean()
    avg_times.append(total)

plt.plot(tb_sizes, avg_times, 'o-')
plt.xlabel('Transport Block Size (bytes)')
plt.ylabel('Average Total Time (microseconds)')
plt.title('Processing Time vs Transport Block Size')
plt.tight_layout()
plt.savefig('tb_size_comparison.png', dpi=150)
plt.show()
```

### Option 2: gnuplot

**Bar chart — average time per layer (one-liner):**
```bash
# First, compute averages from the CSV
awk -F',' 'NR>1 {
    ptx+=$2; rtx+=$3; mtx+=$4; mrx+=$5; rrx+=$6; prx+=$7; n++
} END {
    print "PDCP", ptx/n, prx/n;
    print "RLC", rtx/n, rrx/n;
    print "MAC", mtx/n, mrx/n
}' profiling_results.csv > avg_timing.dat

# Then plot
gnuplot -e "
set terminal png size 800,500;
set output 'layer_timing.png';
set style data histogram;
set style histogram clustered gap 1;
set style fill solid 0.8;
set ylabel 'Time (us)';
set title 'Average Processing Time per Layer';
plot 'avg_timing.dat' using 2:xtic(1) title 'Uplink (TX)', '' using 3 title 'Downlink (RX)'
"
```

**Line chart — per-packet timing:**
```bash
gnuplot -e "
set terminal png size 800,500;
set output 'per_packet_timing.png';
set datafile separator ',';
set xlabel 'Packet';
set ylabel 'Time (us)';
set title 'Per-Packet Processing Time';
set key outside;
plot 'profiling_results.csv' skip 1 using 1:(\$2+\$3+\$4) with lines title 'Uplink', \
     '' skip 1 using 1:(\$5+\$6+\$7) with lines title 'Downlink'
"
```

---

## Tips

- **Discard the first few packets** when computing averages — the first iteration often includes cache warm-up overhead.
- **Run multiple times** and average across runs for more stable results.
- **Use `--num-packets 100`** or more for meaningful statistics.
- On WSL, building and running on the native Linux filesystem (`~/`) is faster than on `/mnt/c/`.
