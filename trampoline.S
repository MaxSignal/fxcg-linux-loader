/*
 * Hand-over trampoline. Copied to the SH7305 IL memory (0xE5200000) and
 * entered with r4 = boot table (also in IL memory, see loader.h):
 *
 *	u32 dest;		uncached (P2) address of the image in RAM
 *	u32 entry;		where to jump once it is there
 *	u32 nchunks;
 *	struct { u32 src, len; } chunk[nchunks];	P2 sources, len % 4 == 0
 *
 * It runs from on-chip memory so it can switch the MMU off under its own
 * feet, silences the hardware the Casio OS left running, drops all cache
 * contents (the loader wrote back the staged image beforehand), gathers
 * the scattered image into one contiguous block and jumps to it.
 * Position independent: only PC-relative literals, no calls.
 */
	.text
	.align	2
	.global	tramp_start
	.global	tramp_end
tramp_start:
	mov	r4, r8			! r4 is banked, r8 is not

	mov.l	.Lsr, r0		! MD=1 RB=0 BL=1 IMASK=15
	ldc	r0, sr

	! INTC: every IPR field to 0, every IMR bit set
	mov.l	.Lintc, r1
	mov	#12, r2
	mov	#0, r0
1:	mov.w	r0, @r1
	dt	r2
	bf/s	1b
	 add	#4, r1
	mov.l	.Limr, r1
	mov	#13, r2
	mov	#-1, r0
2:	mov.b	r0, @r1
	dt	r2
	bf/s	2b
	 add	#4, r1

	! stop TMU0-2 and the RCLK watchdog
	mov.l	.Ltstr, r1
	mov	#0, r0
	mov.b	r0, @r1
	mov.l	.Lrwtcsr, r1
	mov.w	.Lrwtstop, r0
	mov.w	r0, @r1

	! MMU off, UTLB invalidated
	mov.l	.Lmmucr, r1
	mov	#4, r0			! TI=1, AT=0
	mov.l	r0, @r1

	! invalidate operand and instruction caches (OCI | ICI), keep them on
	mov.l	.Lccr, r1
	mov.l	@r1, r0
	mov.w	.Lccrinv, r2
	or	r2, r0
	mov.l	r0, @r1
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	! gather the chunks into dest
	mov.l	@r8+, r9		! dest
	mov.l	@r8+, r10		! entry
	mov.l	@r8+, r11		! nchunks
3:	tst	r11, r11
	bt	5f
	mov.l	@r8+, r1		! src
	mov.l	@r8+, r2		! len
4:	mov.l	@r1+, r0
	mov.l	r0, @r9
	add	#-4, r2
	tst	r2, r2
	bf/s	4b
	 add	#4, r9
	bra	3b
	 add	#-1, r11

5:	jmp	@r10
	 nop

	.align	2
.Lsr:		.long	0x500000f0
.Lintc:		.long	0xa4080000
.Limr:		.long	0xa4080080
.Ltstr:		.long	0xa4490004
.Lrwtcsr:	.long	0xa4520004
.Lmmucr:	.long	0xff000010
.Lccr:		.long	0xff00001c
.Lrwtstop:	.word	0xa500
.Lccrinv:	.word	0x0808
	.align	2
tramp_end:

	.section .note.GNU-stack, "", %progbits
