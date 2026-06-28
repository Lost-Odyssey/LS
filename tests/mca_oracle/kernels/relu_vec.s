.intel_syntax noprefix
# ReLU: max(x,0) with a preloaded zero in zmm0, load+max+store.
vmaxps zmm1, zmm0, zmmword ptr [rsi + rcx]
vmovups zmmword ptr [rdi + rcx], zmm1
