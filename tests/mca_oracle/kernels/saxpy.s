.intel_syntax noprefix
# SAXPY y=a*x+y, 512-bit: load x, fused-load+fma into y, store y. The mem-form FMA
# micro-fuses (fma p0 + load p23); load + store balance -> RThroughput 1.0.
vmovups zmm1, zmmword ptr [rdx + rcx]
vfmadd213ps zmm1, zmm0, zmmword ptr [r8 + rcx]
vmovups zmmword ptr [r8 + rcx], zmm1
