	.globl	videotiler_asm

//	r3 = dst
//	r4 = src0
//	r5 = src1
//	r6 = src2
//	r7 = src3
//	r8 = height
//	r9 = width
//	r10 = srcoffset
	
videotiler_asm:
	addi		11,0,8
	addi		3,3,-8
	addi		4,4,-8
	addi		5,5,-8
	addi		6,6,-8
	addi		7,7,-8
	
1:	
	mtctr	9
2:
	dcbz		11,3
	lfdu		0,8(4)
	lfdu		1,8(5)
	lfdu		2,8(6)
	lfdu		3,8(7)

	stfdu		0,8(3)
	stfdu		1,8(3)
	stfdu		2,8(3)
	stfdu		3,8(3)
	dcbf		0,3
		
	bdnz		2b

	add		4,4,10
	add		5,5,10
	add		6,6,10
	add		7,7,10
	addic.		8,8,-1
	bne+		1b
	blr
