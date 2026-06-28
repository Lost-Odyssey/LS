.intel_syntax noprefix
# classic dot-product accumulate, SINGLE accumulator zmm0 -> loop-carried recurrence
# (zmm0 read+written every iter). Block RThroughput says 1.0 but the FMA latency-4
# recurrence forces ~4 cyc/iter (Total Cycles). The RecMII gap this kernel set adds.
vfmadd231ps zmm0, zmm1, zmm2
