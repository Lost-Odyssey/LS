.intel_syntax noprefix
# streaming copy: one 512-bit load + one 512-bit store per iter.
vmovups zmm0, zmmword ptr [rsi + rcx]
vmovups zmmword ptr [rdi + rcx], zmm0
