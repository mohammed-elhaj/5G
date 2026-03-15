# Git Workflow Guide — 5G Layer 2 Project

A practical crash course. No theory you won't use this week.

---

## Core Concepts in 60 Seconds

**Repository (repo):** The project folder tracked by Git. Everyone has a full copy on their machine.

**Commit:** A snapshot of your changes. Like a save point you can always go back to.

**Branch:** A parallel version of the code. You work on your branch without affecting anyone else. When you're done, you merge it into `main`.

**main branch:** The stable version of the project. V1 lives here. Nobody pushes to it directly — you merge through Pull Requests.

**Pull Request (PR):** A request to merge your branch into `main`. Someone reviews it before it goes in.

**Merge conflict:** When two people edited the same line in the same file. Git can't decide which version to keep, so you resolve it manually.

```
main          ●───●───●─────────────────●───● (stable, protected)
                  │                     ↑
feat/pdcp         ├───●───●───●─────────┘ (Pair A merges via PR)
                  │
feat/rlc          ├───●───●───●───●       (Pair B working)
                  │
feat/mac          ├───●───●               (Pair C working)
                  │
feat/testing      └───●───●───●           (Pair D working)
```

---

## One-Time Setup (Everyone Does This Once)

### 1. Install Git

```bash
# Ubuntu / WSL
sudo apt update
sudo apt install git

# Verify
git --version
```

### 2. Configure Your Identity

```bash
git config --global user.name "Your Full Name"
git config --global user.email "your.email@university.edu"
```

This tags your commits with your name — important because the professor can see who did what.

### 3. Clone the Repo

```bash
# Go to where you want the project folder
cd ~/projects

# Clone (replace URL with your actual repo)
git clone https://github.com/your-team/5g-layer2.git

# Enter the project
cd 5g-layer2
```

Now you have the full project with all V1 code on your machine.

### 4. Verify V1 Works

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./5g_layer2
# Should see: 10/10 packets PASS

# Run tests
./test_pdcp && ./test_rlc && ./test_mac && ./test_integration
# Should see: all 23 tests pass
```

If this doesn't work, fix it before doing anything else.

---

## Daily Workflow

### Starting Your Day

```bash
# 1. Make sure you're on your pair's branch
git checkout feat/pdcp          # or feat/rlc, feat/mac, feat/testing

# 2. Pull latest changes from your partner (if they pushed)
git pull origin feat/pdcp

# 3. Pull latest main (in case another pair merged early)
git pull origin main
```

### While Working

Save your progress frequently. Don't wait until the end of the day to commit.

```bash
# See what you've changed
git status

# See the actual line-by-line changes
git diff

# Stage specific files you want to commit
git add src/pdcp.cpp include/pdcp.h

# Or stage everything you changed
git add -A

# Commit with a descriptive message
git commit -m "[PDCP] Replace XOR cipher with AES-128-CTR using OpenSSL"
```

**Commit message format we agreed on:**
```
[LAYER] Short description

- Detail about what changed
- Another detail

Files changed: src/pdcp.cpp, include/pdcp.h
```

Real examples:
```
[PDCP] Add HMAC-SHA256 integrity verification

- Uses OpenSSL HMAC() with SHA256
- Truncates to 4 bytes for MAC-I field
- Falls back to CRC32 when integrity_algorithm = 0

Files changed: src/pdcp.cpp
```

```
[RLC] Implement AM mode TX with retransmission buffer

- New code path when rlc_mode = 2
- Stores transmitted PDUs in map keyed by SN
- UM mode code unchanged

Files changed: src/rlc.cpp, include/rlc.h
```

```
[MAC] Add multi-LCID multiplexing support

- process_tx now accepts map<uint8_t, vector<ByteBuffer>>
- Backward-compatible: single LCID still works with old interface
- Each SDU gets subheader with correct LCID

Files changed: src/mac.cpp, include/mac.h
```

### Pushing Your Commits to GitHub

```bash
# Push your branch to the remote repo
git push origin feat/pdcp
```

Your partner can now pull your changes with `git pull origin feat/pdcp`.

### End of Day Routine

```bash
# 1. Make sure everything compiles
cd build && make -j$(nproc)

# 2. Run your layer's tests
./test_pdcp       # or whichever is yours

# 3. Run V1 integration tests (make sure you didn't break anything)
./test_integration

# 4. Commit and push
git add -A
git commit -m "[PDCP] End of day — AES cipher working, HMAC in progress"
git push origin feat/pdcp
```

---

## Working as a Pair on the Same Branch

Both members of a pair push to the same branch. The rule is simple: **pull before you push.**

```
Member 1                              Member 2
─────────                              ─────────
writes AES cipher code                writes compression code
git add -A                            git add -A
git commit -m "[PDCP] Add AES"        git commit -m "[PDCP] Add compression"
git push origin feat/pdcp             git pull origin feat/pdcp    ← PULL FIRST
                                      git push origin feat/pdcp
```

If you both edited the SAME file, `git pull` might give you a merge conflict. That's normal — see the "Handling Merge Conflicts" section below.

**Tip to avoid conflicts within a pair:** Agree on which functions each person touches. Member 1 edits `apply_cipher()`. Member 2 edits `compress_header()`. If you're both in `pdcp.cpp` but in different functions, Git handles it cleanly.

---

## Merge Day (Day 4) — Step by Step

### Order of Merging

Merge branches one at a time into `main` in this order:

1. `feat/testing` first (infrastructure, no protocol changes)
2. `feat/pdcp` second
3. `feat/rlc` third
4. `feat/mac` last

After each merge, everyone pulls the updated `main` before the next merge.

### How to Merge via Pull Request

**Step 1:** Go to your repo on GitHub.

**Step 2:** Click "Pull Requests" → "New Pull Request".

**Step 3:** Set base: `main` ← compare: `feat/pdcp`.

**Step 4:** Write a short description of what your pair did.

**Step 5:** Request a review from Pair D (or anyone).

**Step 6:** Reviewer checks:
- Does it compile?
- Do all 23 V1 tests still pass?
- Do the new tests pass?
- No files outside your ownership were modified?

**Step 7:** Reviewer approves → click "Merge Pull Request" → "Confirm Merge".

**Step 8:** Everyone pulls the updated main:
```bash
git checkout main
git pull origin main
```

**Step 9:** Next pair merges. Repeat.

### After All Merges

```bash
# Everyone does this
git checkout main
git pull origin main

# Rebuild everything
cd build
cmake ..
make -j$(nproc)

# Run ALL tests
./test_pdcp && ./test_rlc && ./test_mac && ./test_integration

# Run full profiling
./5g_layer2 --packet-size 1400 --tb-size 2048 --num-packets 50
```

---

## Handling Merge Conflicts

A conflict looks like this when you run `git pull` or try to merge:

```
CONFLICT (content): Merge conflict in include/common.h
Automatic merge failed; fix conflicts and then commit the result.
```

Open the file. Git marks the conflict:

```cpp
struct Config {
    // ... existing fields ...

<<<<<<< HEAD
    // Added by Pair A
    uint8_t cipher_algorithm = 0;
=======
    // Added by Pair B
    double loss_rate = 0.0;
>>>>>>> feat/rlc
};
```

**What this means:** Your version (HEAD) has one change, the other branch has a different change at the same spot.

**How to fix it:** Keep BOTH changes, remove the conflict markers:

```cpp
struct Config {
    // ... existing fields ...

    // Added by Pair A
    uint8_t cipher_algorithm = 0;

    // Added by Pair B
    double loss_rate = 0.0;
};
```

Then:
```bash
git add include/common.h
git commit -m "Resolve merge conflict in common.h — keep both Config fields"
```

**Most common conflict spots in our project:**
- `include/common.h` — multiple pairs adding Config fields (easy fix: keep all additions)
- `CMakeLists.txt` — multiple pairs adding test targets or libraries (easy fix: keep all additions)
- `src/main.cpp` — multiple pairs adding command-line flags (coordinate with Pair D)

---

## Useful Commands Reference

### Checking Status

```bash
git status                    # What files changed
git log --oneline -10         # Last 10 commits, one line each
git log --oneline --graph     # Visual branch history
git branch                    # List local branches (* marks current)
git branch -a                 # List all branches including remote
```

### Undoing Mistakes

```bash
# Undo changes to a file (before committing)
git checkout -- src/pdcp.cpp

# Unstage a file (after git add, before commit)
git reset HEAD src/pdcp.cpp

# Undo your last commit (keep the changes as uncommitted)
git reset --soft HEAD~1

# NUCLEAR OPTION: discard ALL uncommitted changes
git reset --hard HEAD
# WARNING: this deletes your work permanently
```

### Comparing Versions

```bash
# See what changed between your branch and main
git diff main..feat/pdcp

# See what a specific commit changed
git show abc1234

# See who last edited each line of a file
git blame src/pdcp.cpp
```

### Stashing (temporarily saving work)

```bash
# You need to switch branches but have uncommitted changes
git stash                     # Saves your changes temporarily
git checkout main             # Switch to main
git checkout feat/pdcp        # Come back
git stash pop                 # Restore your saved changes
```

---

## Quick Cheat Sheet — Copy This to Your Desktop

```
DAILY START:
  git checkout feat/<your-layer>
  git pull origin feat/<your-layer>
  git pull origin main

WHILE WORKING:
  git add -A
  git commit -m "[LAYER] What I did"

END OF DAY:
  make && ./test_<your-layer> && ./test_integration
  git add -A
  git commit -m "[LAYER] Summary of today"
  git push origin feat/<your-layer>

MERGE DAY:
  Go to GitHub → Pull Request → main ← feat/<your-layer>
  Wait for review → Merge
  git checkout main && git pull origin main
```

---

## Rules (From AI_RULES.md — Repeated Here for Emphasis)

1. **Never push to `main` directly.** Always use a Pull Request.
2. **Pull before you push** to your pair's branch.
3. **Commit often** with `[LAYER]` prefixed messages.
4. **Don't commit build artifacts.** The `.gitignore` handles this, but double-check that `build/` folder contents never show up in `git status`.
5. **If you break something, tell the group chat** before pushing.
6. **If you change a header file interface, tell everyone** before pushing.
