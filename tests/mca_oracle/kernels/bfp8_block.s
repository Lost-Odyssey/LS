	.text
	.globl	bfp8_block
# clean-room BFP8 per-block kernel (one scan iteration = loaded-signal case).
# zmm0 = 32 int16 samples; zmm16 = 0x8000 MSB mask (loop-invariant, pre-loaded).
bfp8_block:
	vpabsw	%zmm0, %zmm1
	xorl	%ecx, %ecx
	vptestmw	%zmm16, %zmm1, %k1
	kortestw	%k1, %k1
	jnz	1f
	vpsllw	$1, %zmm1, %zmm1
	incl	%ecx
	cmpl	$9, %ecx
1:
	movl	$9, %edx
	subl	%ecx, %edx
	vmovd	%edx, %xmm3
	vpsraw	%xmm3, %zmm0, %zmm4
	vpmovwb	%zmm4, %ymm5
	movb	%dl, (%rdi)
	vmovdqu8	%ymm5, 1(%rdi)
