/*
 * Add-in start-up code (after the libfxcg crt0 by Tari and calc84maniac).
 * The Casio OS maps the code of the .g3a at 0x00300000 and 512 KiB of
 * RAM at 0x08100000, then calls the first byte of the code.
 */
	.section ".pretext", "ax"
	.align	2
	.global	initialize
initialize:
	mov.l	r14, @-r15
	sts.l	pr, @-r15
	mov.l	r4, @-r15

	! copy .data into RAM
	mov.l	v_datald, r0
	mov.l	v_sdata, r2
	mov.l	v_edata, r3
1:	cmp/hs	r3, r2
	bt	2f
	mov.l	@r0+, r1
	mov.l	r1, @r2
	bra	1b
	 add	#4, r2
2:
	! clear .bss
	mov.l	v_ebss, r2
	mov.l	v_bbss, r3
	mov	#0, r1
3:	cmp/hi	r3, r2
	bf	4f
	bra	3b
	 mov.b	r1, @-r2
4:
	mov	r5, r14
	mov	#1, r6
	mov	#0, r4
	mov.l	v_syscall, r2
	mov	#0x29, r0		! GlibAddinAplExecutionCheck(0, 1, 1)
	jsr	@r2
	 mov	r6, r5

	mov.l	v_main, r7
	extu.w	r14, r5
	mov.l	@r15+, r4
	lds.l	@r15+, pr
	jmp	@r7
	 mov.l	@r15+, r14

	.align	2
v_main:		.long	main
v_syscall:	.long	0x80020070
v_datald:	.long	datald
v_sdata:	.long	sdata
v_edata:	.long	edata
v_bbss:		.long	bbss
v_ebss:		.long	ebss

	.section .note.GNU-stack, "", %progbits
