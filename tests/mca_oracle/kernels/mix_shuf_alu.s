	.text
	.globl	mix_shuf_alu
mix_shuf_alu:
	vpshufb	%zmm1, %zmm0, %zmm2
	vpaddd	%zmm4, %zmm3, %zmm5
	vpshufb	%zmm7, %zmm6, %zmm8
	vpaddd	%zmm10, %zmm9, %zmm11
	vpshufb	%zmm13, %zmm12, %zmm14
	vpaddd	%zmm16, %zmm15, %zmm17
