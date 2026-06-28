#!/bin/bash
# Regenerate the llvm-mca golden files for the mca_oracle reference kernels.
# Run this when a kernel .s changes (requires llvm-mca). The LS regression test
# (tests/samples/sim_mca_oracle_test.ls) carries the key numbers hardcoded with
# provenance; these .golden files are the full llvm-mca output they cite.
#
#   LLVM_MCA=/path/to/llvm-mca.exe ./regen_golden.sh
set -e
MCA="${LLVM_MCA:-/c/llvm/bin/llvm-mca.exe}"
CPU="${MCA_CPU:-icelake-server}"
ITERS="${MCA_ITERS:-1000}"   # fixed iteration count so Total/ITERS ≈ steady cyc/iter
cd "$(dirname "$0")/kernels"
for s in *.s; do
    k="${s%.s}"
    {
        echo "# llvm-mca -mcpu=$CPU -iterations=$ITERS $s  (regen: tests/mca_oracle/regen_golden.sh)"
        # Block RThroughput = pure port/throughput bound (ignores loop-carried recurrence).
        # Total Cycles over $ITERS iterations = the ACTUAL steady-state cost, which DOES
        # include the loop-carried recurrence (RecMII) — Total/ITERS ≈ cyc/iter. The two
        # diverge exactly for recurrence-bound kernels (dot_serial, reduce_serial), which
        # is the signal the RecMII engine layer is calibrated against.
        echo "# ITERATIONS=$ITERS"
        "$MCA" -mcpu="$CPU" -iterations="$ITERS" "$s" 2>&1 | grep -iE "Total Cycles|Block RThroughput"
        echo "# Resource pressure per iteration ([2]=Port0 [3]=Port1 [4]=P2ld [5]=P3ld [6]=P4st [7]=Port5 [9..11]=P7/8/9 st-AGU):"
        "$MCA" -mcpu="$CPU" -iterations="$ITERS" "$s" 2>&1 | sed -n '/Resource pressure per iteration/,/^$/p' | grep -vE '^\s*$'
    } > "$k.golden"
    echo "regenerated $k.golden"
done
