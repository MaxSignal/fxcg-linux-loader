/*
 * fxcg-linux loader: boots a Linux zImage on the Casio fx-CG10/fx-CG20.
 *
 * The add-in reads the kernel file (\\fls0\linux.bin, or linuxf.bin)
 * into the RAM the Casio OS leaves to an add-in (the free part of the
 * add-in RAM, blocks of the OS heap, the VRAM), records the physical
 * address of every page from the UTLB, writes the data back from the
 * cache and hands over to trampoline.S, run from the on-chip IL memory,
 * which switches the MMU off, gathers the pages at 0x08040000 and jumps
 * to them.
 *
 * linuxf.bin has part of its code in the flash at 0xc00000 and 0xb20000.
 * F2 boots it only when the trailer at its end matches each area's
 * header and the CRC of its contents; otherwise, with \\fls0\flash.bin
 * there, linux.bin is booted to install it. F6 boots linux.bin to remove
 * it. The word at the end of the IL memory tells the kernel which.
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* Casio OS syscalls (syscalls.S) */
void Bdisp_AllClr_VRAM(void);
void Bdisp_PutDisp_DD(void);
void Bdisp_EnableColor(int on);
void EnableStatusArea(int mode);
void PrintXY(int x, int y, const char *s, int mode, int color);
int GetKey(int *key);
void *GetVRAMAddress(void);
int Bfile_OpenFile_OS(const u16 *name, int mode, int zero);
int Bfile_CloseFile_OS(int handle);
int Bfile_GetFileSize_OS(int handle);
int Bfile_ReadFile_OS(int handle, void *buf, int size, int readpos);
void *sys_malloc(int size);

#define KEY_CTRL_EXIT	30002
#define KEY_CTRL_EXE	30004
#define KEY_CTRL_F1	30009
#define KEY_CTRL_F2	30010
#define KEY_CTRL_F6	30014

#define COLOR_BLACK	0
#define COLOR_BLUE	1
#define COLOR_RED	4

/* trampoline.S */
extern const u8 tramp_start[], tramp_end[];

/* linker script */
extern u8 ebss[];

#define PAGE		4096
#define ADDIN_RAM	0x08100000u	/* virtual, 512 KiB */
#define ADDIN_RAM_END	0x08180000u
#define STACK_MARGIN	0x8000u		/* keep 32 KiB below the stack */

#define RAM_BASE	0x08000000u	/* fx-CG10/20: 2 MiB */
#define RAM_END		0x08200000u
#define VRAM_SIZE	(384 * 216 * 2)

#define IL_MEM		0xe5200000u	/* 4 KiB on-chip IL memory */
#define IL_TABLE	(IL_MEM + 0x200)
#define IL_MODE		(IL_MEM + 0xff0)	/* "fxcgMODE", mode */
#define IL_END		IL_MODE

#define DEST		0x08040000u	/* where the zImage is gathered */
#define P2(pa)		((pa) | 0xa0000000u)

#define MAX_CHUNKS	((IL_END - IL_TABLE) / 8 - 2)

struct chunk {
	u32 src, len;
};

struct boot_table {
	u32 dest, entry, nchunks;
	struct chunk chunk[];
};

static struct chunk chunks[MAX_CHUNKS];
static u32 nchunks;
static int line = 1;

/* the compiler may emit calls to these */
void *memset(void *d, int c, unsigned long n)
{
	u8 *p = d;

	while (n--)
		*p++ = c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	u8 *p = d;
	const u8 *q = s;

	while (n--)
		*p++ = *q++;
	return d;
}

int memcmp(const void *a, const void *b, unsigned long n)
{
	const u8 *p = a, *q = b;

	for (; n; n--, p++, q++)
		if (*p != *q)
			return *p - *q;
	return 0;
}

/* ------------------------------------------------------------- output */

static void print(const char *s, int color)
{
	char buf[32] = "  ";
	int i;

	for (i = 0; s[i] && i < 21; i++)
		buf[i + 2] = s[i];
	buf[i + 2] = 0;
	if (line > 8) {
		Bdisp_AllClr_VRAM();
		line = 1;
	}
	PrintXY(1, line++, buf, 0, color);
	Bdisp_PutDisp_DD();
}

/* decimal without division (libgcc is built for FDPIC user space) */
static char *udec(char *p, u32 v)
{
	static const u32 pow10[] = { 1000000000, 100000000, 10000000, 1000000,
				     100000, 10000, 1000, 100, 10, 1 };
	int i, started = 0;

	for (i = 0; i < 10; i++) {
		char d = '0';

		while (v >= pow10[i]) {
			v -= pow10[i];
			d++;
		}
		if (d != '0' || started || i == 9) {
			*p++ = d;
			started = 1;
		}
	}
	*p = 0;
	return p;
}

static char *str(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	*p = 0;
	return p;
}

static void print_kib(const char *what, u32 bytes, int color)
{
	char buf[32], *p = str(buf, what);

	p = udec(p, (bytes + 1023) >> 10);
	str(p, " KiB");
	print(buf, color);
}

static void __attribute__((noreturn)) fail(const char *why)
{
	int key;

	print(why, COLOR_RED);
	print("Press MENU to leave.", COLOR_BLACK);
	for (;;)
		GetKey(&key);
}

/* ---------------------------------------------------------------- MMU */

#define UTLB_ADDR(i)	(*(volatile u32 *)(0xf6000000u | ((i) << 8)))
#define UTLB_DATA(i)	(*(volatile u32 *)(0xf7000000u | ((i) << 8)))
#define PTEH		(*(volatile u32 *)0xff000000u)

/* Physical address of a mapped virtual address, 0 if not in the UTLB. */
static u32 utlb_lookup(u32 va)
{
	u32 asid = PTEH & 0xff;
	int i;

	for (i = 0; i < 64; i++) {
		u32 a = UTLB_ADDR(i), d = UTLB_DATA(i), size, mask;

		if (!(a & 0x100) || !(d & 0x100))
			continue;
		if (!(d & 0x2) && (a & 0xff) != asid)
			continue;
		switch (((d >> 6) & 2) | ((d >> 4) & 1)) {
		case 0:  size = 0x400; break;
		case 1:  size = 0x1000; break;
		case 2:  size = 0x10000; break;
		default: size = 0x100000; break;
		}
		mask = ~(size - 1);
		if ((a & mask) == (va & mask))
			return (d & 0x1ffffc00u & mask) | (va & ~mask);
	}
	return 0;
}

static u32 virt_to_phys(u32 va)
{
	int tries;

	if (va >= 0x80000000u && va < 0xc0000000u)
		return va & 0x1fffffffu;
	for (tries = 0; tries < 4; tries++) {
		u32 pa;

		(void)*(volatile u32 *)va;	/* make the OS load the entry */
		pa = utlb_lookup(va);
		if (pa)
			return pa;
	}
	return 0;
}

static void cache_writeback(u32 va, u32 len)
{
	u32 end = va + len;

	for (va &= ~31u; va < end; va += 32)
		__asm__ volatile("ocbwb @%0" : : "r"(va) : "memory");
}

static void add_chunk(u32 pa, u32 len)
{
	if (nchunks && chunks[nchunks - 1].src + chunks[nchunks - 1].len == P2(pa)) {
		chunks[nchunks - 1].len += len;
		return;
	}
	if (nchunks == MAX_CHUNKS)
		fail("Image too fragmented");
	chunks[nchunks].src = P2(pa);
	chunks[nchunks].len = len;
	nchunks++;
}

static int overlaps_dest(u32 pa, u32 len, u32 size)
{
	return pa < DEST + size && DEST < pa + len;
}

/*
 * Read file data into [va, va + len) page by page, recording the physical
 * pages. Returns the number of bytes staged (stops early at the end of
 * the file or at a page that cannot be translated).
 */
static u32 stage(int fd, u32 va, u32 len, u32 *done, u32 size)
{
	u32 staged = 0;

	while (staged < len && *done < size) {
		u32 room = PAGE - (va & (PAGE - 1)), n, pa;

		if (room > len - staged)
			room = len - staged;
		n = size - *done < room ? size - *done : room;
		pa = virt_to_phys(va);
		if (!pa)
			break;
		if (overlaps_dest(pa, n, size))
			fail("RAM layout unexpected");
		if (Bfile_ReadFile_OS(fd, (void *)va, n, *done) != (int)n)
			fail("read error");
		cache_writeback(va, n);
		add_chunk(pa, n);
		*done += n;
		staged += n;
		va += n;
	}
	return staged;
}

/* ------------------------------------------------- kernel in the flash */

/* fxcg20-Linux-bundle scripts/romkernel.py, arch/sh/boards/mach-fxcg/xipflash.c */
#define NR_AREAS	2
static const u32 area_start[NR_AREAS] = { 0x00c00000, 0x00b20000 };
static const u32 area_size[NR_AREAS] = { 0x80000, 0x20000 };

static const char kernel_path[] = "\\\\fls0\\linux.bin";
static const char linuxf_path[] = "\\\\fls0\\linuxf.bin";
static const char flash_path[] = "\\\\fls0\\flash.bin";

static u32 be32(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

/* CRC-32 (as zlib's), with a 16-entry table */
static u32 crc32(const u8 *p, u32 n)
{
	static const u32 t[16] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
	};
	u32 c = 0xffffffffu;

	while (n--) {
		c ^= *p++;
		c = (c >> 4) ^ t[c & 15];
		c = (c >> 4) ^ t[c & 15];
	}
	return ~c;
}

static int open_file(const char *path)
{
	u16 name[32];
	int i;

	for (i = 0; path[i] && i < 31; i++)
		name[i] = (u8)path[i];
	name[i] = 0;
	return Bfile_OpenFile_OS(name, 0, 0);
}

static int file_exists(const char *path)
{
	int fd = open_file(path);

	if (fd < 0)
		return 0;
	Bfile_CloseFile_OS(fd);
	return 1;
}

/* Does area i start with a header written by the installer? */
static int area_ours(int i)
{
	const u8 *h = (const u8 *)P2(area_start[i]);

	return !memcmp(h, "fxcgROM1", 8) && crc32(h, 60) == be32(h + 60) &&
	       be32(h + 8) == area_start[i];
}

/*
 * 1 when the flash holds the part linuxf.bin was built with, 0 when it
 * does not, -1 when there is no linuxf.bin.
 */
static int rom_matches(void)
{
	u8 t[64];
	int fd = open_file(linuxf_path), size, ok, i;

	if (fd < 0)
		return -1;
	size = Bfile_GetFileSize_OS(fd);
	ok = size > 64 && Bfile_ReadFile_OS(fd, t, 64, size - 64) == 64;
	Bfile_CloseFile_OS(fd);
	if (!ok || memcmp(t, "fxcgROMK", 8) || crc32(t, 60) != be32(t + 60) ||
	    be32(t + 8) != NR_AREAS)
		return 0;
	for (i = 0; i < NR_AREAS; i++) {
		u32 addr = be32(t + 12 + 12 * i), len = be32(t + 16 + 12 * i);
		u32 crc = be32(t + 20 + 12 * i);
		const u8 *h = (const u8 *)P2(area_start[i]);

		if (addr != area_start[i] || len < 64 || len > area_size[i] ||
		    !area_ours(i) || be32(h + 12) != len || be32(h + 16) != crc ||
		    crc32(h + 64, len - 64) != crc)
			return 0;
	}
	return 1;
}

/* what the kernel is to do, in the word at the end of the IL memory */
enum { MODE_RUN, MODE_INSTALL, MODE_REMOVE };

static void __attribute__((noreturn)) boot(u32 entry, u32 mode)
{
	volatile u32 *m = (volatile u32 *)IL_MODE;
	struct boot_table *t;
	int i;

	m[0] = 0x66786367;	/* "fxcgMODE" */
	m[1] = 0x4d4f4445;
	m[2] = mode;
	/* trampoline and boot table into the IL memory */
	for (i = 0; i < tramp_end - tramp_start; i++)
		((volatile u8 *)IL_MEM)[i] = tramp_start[i];
	t = (struct boot_table *)IL_TABLE;
	t->dest = P2(DEST);
	t->entry = entry;
	t->nchunks = nchunks;
	for (i = 0; i < (int)t->nchunks; i++)
		t->chunk[i] = chunks[i];

	((void (*)(struct boot_table *))IL_MEM)(t);
	for (;;)
		;
}

/* ---------------------------------------------------------------- main */

int main(void)
{
	u32 size, done, va, sp, sp_pa, pa, n, end, heap, mode = MODE_RUN;
	int fd, key, rom, have_flash, present, can_f2;
	const char *path = kernel_path;

	Bdisp_EnableColor(1);
	EnableStatusArea(3);
	Bdisp_AllClr_VRAM();
	print("fxcg-linux loader", COLOR_BLUE);

	/* fx-CG10/20 keep RAM at 0x08000000; the fx-CG50 does not */
	pa = virt_to_phys(ADDIN_RAM);
	if (pa < RAM_BASE || pa >= RAM_END)
		fail("Not an fx-CG10/20");

	rom = rom_matches();
	have_flash = file_exists(flash_path);
	present = area_ours(0) || area_ours(1);
	can_f2 = rom == 1 || (rom == 0 && have_flash);
	print("F1: Linux in RAM", COLOR_BLUE);
	if (rom == 1)
		print("F2: Linux (flash)", COLOR_BLUE);
	else if (can_f2)
		print("F2: install flash.bin", COLOR_BLUE);
	if (present)
		print("F6: remove from flash", COLOR_BLACK);
	print("Main Memory is lost.", COLOR_RED);
	/* EXE: F2 when there is something to boot or install there */
	for (;;) {
		GetKey(&key);
		if (key == KEY_CTRL_EXE)
			key = can_f2 ? KEY_CTRL_F2 : KEY_CTRL_F1;
		if (key == KEY_CTRL_F1)
			break;
		if (key == KEY_CTRL_F2 && rom == 1) {
			path = linuxf_path;
			break;
		}
		if (key == KEY_CTRL_F2 && can_f2) {
			mode = MODE_INSTALL;
			break;
		}
		if (key == KEY_CTRL_F6 && present) {
			mode = MODE_REMOVE;
			break;
		}
	}
	Bdisp_AllClr_VRAM();
	line = 1;
	if (mode == MODE_INSTALL)
		print("Linux to install it", COLOR_BLUE);
	if (mode == MODE_REMOVE)
		print("Linux to remove it", COLOR_BLUE);

	fd = open_file(path);
	if (fd < 0)
		fail(path == kernel_path ? "linux.bin not found" : "linuxf.bin not found");
	size = Bfile_GetFileSize_OS(fd);
	if (size < 1024 || (size & 3))
		fail("the kernel file is invalid");
	print_kib("image: ", size, COLOR_BLACK);

	__asm__("mov r15, %0" : "=r"(sp));
	sp_pa = virt_to_phys(sp);

	/* 1. add-in RAM above our .bss, up to 32 KiB below the stack */
	done = 0;
	va = ((u32)ebss + PAGE - 1) & ~(PAGE - 1);
	end = ADDIN_RAM_END;
	if (sp >= ADDIN_RAM && sp < ADDIN_RAM_END)
		end = (sp - STACK_MARGIN) & ~(PAGE - 1);
	for (; va < end && done < size; va += PAGE) {
		pa = virt_to_phys(va);
		if (!pa || (sp_pa && pa + PAGE > sp_pa - STACK_MARGIN && pa < sp_pa))
			break;
		stage(fd, va, PAGE, &done, size);
	}
	print_kib("add-in RAM: ", done, COLOR_BLACK);

	/* 2. the OS heap, in decreasing block sizes */
	heap = done;
	for (n = 128 * 1024; done + VRAM_SIZE < size && n >= PAGE; ) {
		void *p = sys_malloc(n);

		if (!p) {
			n >>= 1;
			continue;
		}
		stage(fd, (u32)p, n, &done, size);
	}
	print_kib("heap: ", done - heap, COLOR_BLACK);

	/* 3. the VRAM (start of the RAM), filled at the very end */
	if (size - done > VRAM_SIZE)
		fail("the kernel is too large");
	if (DEST + size > RAM_END)
		fail("the kernel is too large");

	/* a fresh screen: these two lines stay up while Linux decompresses */
	Bdisp_AllClr_VRAM();
	line = 1;
	print("Booting Linux...", COLOR_BLUE);
	print("(RESET to go back)", COLOR_BLACK);

	/*
	 * The rest goes to the VRAM, which is the start of the RAM. Nothing
	 * may be drawn from here on.
	 */
	if (done < size) {
		n = size - done;
		pa = virt_to_phys((u32)GetVRAMAddress());
		if (overlaps_dest(pa, n, size))
			fail("RAM layout unexpected");
		stage(fd, P2(pa), n, &done, size);
	}
	Bfile_CloseFile_OS(fd);
	boot(DEST, mode);
}
