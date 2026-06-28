	.text
	.globl	fma_indep
fma_indep:
	vfmadd231ps	%zmm2, %zmm1, %zmm0
	vfmadd231ps	%zmm5, %zmm4, %zmm3
	vfmadd231ps	%zmm8, %zmm7, %zmm6
	vfmadd231ps	%zmm11, %zmm10, %zmm9
	vfmadd231ps	%zmm14, %zmm13, %zmm12
	vfmadd231ps	%zmm17, %zmm16, %zmm15
