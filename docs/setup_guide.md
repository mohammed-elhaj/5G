# Setup Guide

Step-by-step instructions for building and running the 5G NR Layer 2 simulator on Ubuntu / WSL2.

## Tested Environment

| Component | Version |
|-----------|---------|
| OS | Ubuntu 24.04 LTS (Noble Numbat) on WSL2 |
| Compiler | g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04) |
| CMake | 3.28.3 |
| C++ Standard | **C++17 (minimum requirement)** |

---

## 1. Install WSL2 on Windows

If you don't already have WSL2, open **PowerShell as Administrator** and run:

```powershell
wsl --install
```

This installs Ubuntu by default. Restart your computer when prompted.

After reboot, Ubuntu will launch and ask you to create a username and password. Remember this password -- you'll need it for `sudo` commands.

To verify your installation:

```powershell
wsl --list --verbose
```

You should see Ubuntu listed with VERSION 2.

---

## 2. Install Build Tools

Open your WSL terminal (type `wsl` in PowerShell, or launch the Ubuntu app) and install the required packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev
```

`build-essential` installs g++, gcc, make, and standard C/C++ libraries. `cmake` is the build system generator. `libssl-dev` provides OpenSSL headers and libraries required for PDCP security features (AES-128-CTR and HMAC-SHA256).

Verify the installations:

```bash
g++ --version
# Expected: g++ (Ubuntu 13.x.x ...) 13.x.x or newer

cmake --version
# Expected: cmake version 3.14 or newer

make --version
# Expected: GNU Make 4.x
```

Any g++ version that supports C++17 will work (g++ 7+ or clang++ 5+).

---

## 3. Clone the Repository

Navigate to your working directory and clone:

```bash
# If working from the Windows filesystem (accessible in WSL):
cd /mnt/c/Users/<your-username>/Desktop/

# Clone the repo
git clone <repository-url> 5G
cd 5G
```

Or if you already have the project on your Windows desktop, just navigate to it:

```bash
cd /mnt/c/Users/<your-username>/Desktop/5G
```

---

## 4. Build the Project

Create a build directory and compile:

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

`-j$(nproc)` uses all available CPU cores for a faster build. You should see output ending with:

```
[100%] Built target 5g_layer2
```

This produces 5 executables in the `build/` directory:
- `5g_layer2` — the main simulator
- `test_pdcp` — PDCP layer unit tests
- `test_rlc` — RLC layer unit tests
- `test_mac` — MAC layer unit tests
- `test_integration` — full pipeline integration tests

---

## 5. Run the Simulator

From the `build/` directory:

```bash
# Default settings: 1400-byte packets, 10 packets, 2048-byte TB
./5g_layer2

# Custom parameters
./5g_layer2 --packet-size 500 --num-packets 50 --tb-size 4096
```

A successful run prints `PASS` for every packet and writes timing data to `profiling_results.csv`.

---

## 6. Run the Tests

```bash
# From the build/ directory:

# Run each test suite individually
./test_pdcp           # 6 tests — cipher/integrity round-trip
./test_rlc            # 5 tests — segmentation/reassembly
./test_mac            # 5 tests — multiplexing/demultiplexing
./test_integration    # 7 tests — full pipeline scenarios

# Or run all tests at once (stops on first failure)
./test_pdcp && ./test_rlc && ./test_mac && ./test_integration
```

All 23 tests should pass. If any test fails, the executable returns a non-zero exit code.

---

## 7. Rebuilding After Changes

If you modify source files, rebuild from the `build/` directory:

```bash
cd build
make -j$(nproc)
```

CMake only recompiles changed files. For a clean rebuild:

```bash
cd build
make clean
make -j$(nproc)
```

To start completely fresh:

```bash
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## Troubleshooting

**`cmake: command not found`**
```bash
sudo apt-get install -y cmake
```

**`g++: command not found`**
```bash
sudo apt-get install -y build-essential
```

**`Could NOT find OpenSSL` error during cmake**
```bash
sudo apt-get install -y libssl-dev libssl3
```

**`fatal error: ... .h: No such file or directory`**
Make sure you're running `cmake ..` from inside the `build/` directory, not from the project root.

**WSL password not accepted / characters not appearing**
This is normal Linux behavior -- password characters are hidden when typing. If you've forgotten your WSL password, reset it from PowerShell:
```powershell
wsl -u root passwd <your-username>
```

**Slow builds on WSL with Windows filesystem**
Building on `/mnt/c/...` can be slower than building on the Linux filesystem. For faster builds, copy the project to `~/`:
```bash
cp -r /mnt/c/Users/<username>/Desktop/5G ~/5G
cd ~/5G/build && cmake .. && make -j$(nproc)
```
