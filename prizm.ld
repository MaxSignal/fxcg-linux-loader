/* Casio fx-CG add-in layout: code mapped at 0x00300000, RAM at 0x08100000 */
OUTPUT_ARCH(sh)
ENTRY(initialize)

MEMORY
{
	rom (rx)  : o = 0x00300000, l = 512k
	ram (rwx) : o = 0x08100004, l = 64k
}

SECTIONS
{
	.text : {
		*(.pretext)
		*(.text .text.*)
	} > rom

	.rodata : {
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > rom

	.data : ALIGN(4) {
		datald = LOADADDR(.data);
		sdata = .;
		*(.data .data.*)
		. = ALIGN(4);
		edata = .;
	} > ram AT> rom

	.bss (NOLOAD) : ALIGN(4) {
		bbss = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		ebss = .;
	} > ram

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
