# Team Tasks — Post-V1 Optimization

This document assigns optimization tasks to team members after the V1 baseline is delivered and verified. Fill in names and deadlines after your team meeting.

---

## PDCP Optimization

**Assigned to:** _____________
**Deadline:** _____________

### Tasks

- [ ] Replace XOR stream cipher with AES-128-CTR (use OpenSSL or a lightweight library)
- [ ] Replace CRC32 integrity with CMAC or HMAC-SHA256
- [ ] Add optional ROHC-style header compression (even a simplified version)
- [ ] Profile cipher/integrity performance before and after changes
- [ ] Update `test_pdcp.cpp` to cover the new algorithms
- [ ] Document changes in a short write-up for the report

### Notes

_Add implementation notes, issues, or design decisions here._

---

## RLC Optimization

**Assigned to:** _____________
**Deadline:** _____________

### Tasks

- [ ] Add AM (Acknowledged Mode) support: ARQ retransmission, STATUS PDUs
- [ ] Implement 12-bit SN for UM mode
- [ ] Add t-Reassembly timer logic (timeout for missing segments)
- [ ] Handle out-of-order segment delivery
- [ ] Profile segmentation vs. reassembly costs before and after changes
- [ ] Update `test_rlc.cpp` with AM mode and reordering tests
- [ ] Document changes in a short write-up for the report

### Notes

_Add implementation notes, issues, or design decisions here._

---

## MAC Optimization

**Assigned to:** _____________
**Deadline:** _____________

### Tasks

- [ ] Add support for multiple logical channels (multiplex 2+ LCIDs)
- [ ] Implement basic Logical Channel Prioritization (LCP)
- [ ] Add MAC CE support (at least Buffer Status Report)
- [ ] Support variable transport block sizes
- [ ] Profile multiplexing overhead before and after changes
- [ ] Update `test_mac.cpp` with multi-LCID and MAC CE tests
- [ ] Document changes in a short write-up for the report

### Notes

_Add implementation notes, issues, or design decisions here._

---

## Integration & Report

**Assigned to:** _____________
**Deadline:** _____________

### Tasks

- [ ] Run profiling across various TB sizes (256, 512, 1024, 2048, 4096, 8192)
- [ ] Run profiling across various IP packet sizes (100, 500, 1000, 1400, 2000)
- [ ] Generate profiling charts (gnuplot or Python matplotlib)
- [ ] Write the project report (introduction, architecture, results, conclusions)
- [ ] Write the user manual
- [ ] Ensure build works cleanly on the target Linux environment
- [ ] Merge all team members' branches and verify integration tests pass
- [ ] Final review and cleanup

### Notes

_Add implementation notes, issues, or design decisions here._
