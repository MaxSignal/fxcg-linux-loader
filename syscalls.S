/* Casio OS syscalls: jump to the dispatcher at 0x80020070 with r0 = number */
#define SYSCALL(name, num)	\
	.text;			\
	.align	2;		\
	.global	name;		\
name:				\
	mov.l	1f, r2;		\
	mov.l	2f, r0;		\
	jmp	@r2;		\
	 nop;			\
	.align	2;		\
1:	.long	0x80020070;	\
2:	.long	num

SYSCALL(Bdisp_AllClr_VRAM,	0x0272)
SYSCALL(Bdisp_PutDisp_DD,	0x025F)
SYSCALL(Bdisp_EnableColor,	0x0921)
SYSCALL(EnableStatusArea,	0x02B7)
SYSCALL(PrintXY,		0x18F9)
SYSCALL(GetKey,			0x0EAB)
SYSCALL(GetVRAMAddress,		0x01E6)
SYSCALL(Bfile_OpenFile_OS,	0x1DA3)
SYSCALL(Bfile_CloseFile_OS,	0x1DA4)
SYSCALL(Bfile_GetFileSize_OS,	0x1DA6)
SYSCALL(Bfile_ReadFile_OS,	0x1DAC)
SYSCALL(sys_malloc,		0x1F44)

	.section .note.GNU-stack, "", %progbits
