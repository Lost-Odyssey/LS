.intel_syntax noprefix
# 4 partial accumulators -> 4 independent recurrence chains (each dist 1, lat 4).
# ResMII = 4 FMAs / p0 = 4.0 ; RecMII = 4 (per chain). II = max = 4 for 4 elements
# = 1 cyc/elem: the classic "unroll with N accumulators to hide FMA latency".
vfmadd231ps zmm0, zmm8, zmm9
vfmadd231ps zmm1, zmm8, zmm9
vfmadd231ps zmm2, zmm8, zmm9
vfmadd231ps zmm3, zmm8, zmm9
