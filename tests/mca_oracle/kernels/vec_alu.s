	.text
	.globl	vec_alu
vec_alu:
	vpaddd	%zmm1, %zmm0, %zmm2
	vpaddd	%zmm3, %zmm2, %zmm4
	vpxord	%zmm5, %zmm4, %zmm6
	vpaddd	%zmm7, %zmm6, %zmm8
