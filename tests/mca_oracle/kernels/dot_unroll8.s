.intel_syntax noprefix
# 8 accumulators: ResMII = 8 FMAs / p0 = 8.0 now EXCEEDS RecMII 4 -> port-bound.
# Unrolling past latency/throughput (=4) buys nothing: II=8 for 8 elems = 1 cyc/elem,
# same per-element as unroll4. The advisor's "stop unrolling here" signal.
vfmadd231ps zmm0, zmm8, zmm9
vfmadd231ps zmm1, zmm8, zmm9
vfmadd231ps zmm2, zmm8, zmm9
vfmadd231ps zmm3, zmm8, zmm9
vfmadd231ps zmm4, zmm8, zmm9
vfmadd231ps zmm5, zmm8, zmm9
vfmadd231ps zmm6, zmm8, zmm9
vfmadd231ps zmm7, zmm8, zmm9
