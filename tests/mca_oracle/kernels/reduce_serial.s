.intel_syntax noprefix
# horizontal-sum style: single accumulator vaddps -> loop-carried, lat 4 recurrence.
# Block RThroughput 0.5 (vaddps {0,5}) but ~4 cyc/iter actual (RecMII).
vaddps zmm0, zmm0, zmm1
