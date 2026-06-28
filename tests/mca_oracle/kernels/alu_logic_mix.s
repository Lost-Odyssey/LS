	.text
	.globl	alu_logic_mix
alu_logic_mix:
	vpaddd	%zmm1, %zmm0, %zmm2
	vpxord	%zmm4, %zmm3, %zmm5
	vpaddd	%zmm7, %zmm6, %zmm8
	vpxord	%zmm10, %zmm9, %zmm11
	vpaddd	%zmm13, %zmm12, %zmm14
	vpxord	%zmm16, %zmm15, %zmm17
	vpaddd	%zmm19, %zmm18, %zmm20
	vpxord	%zmm22, %zmm21, %zmm23
