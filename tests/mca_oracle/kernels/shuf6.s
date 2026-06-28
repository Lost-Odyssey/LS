	.text
	.globl	shuf6
shuf6:
	vpshufb	%zmm1, %zmm0, %zmm2
	vpshufb	%zmm4, %zmm3, %zmm5
	vpshufb	%zmm7, %zmm6, %zmm8
	vpshufb	%zmm10, %zmm9, %zmm11
	vpshufb	%zmm13, %zmm12, %zmm14
	vpshufb	%zmm16, %zmm15, %zmm17
