# fxcg-linux-loader

A Casio fx-CG10 / fx-CG20 (Prizm) add-in that boots Linux from the Casio OS.
It is the loader of [fxcg20-Linux-bundle](https://github.com/MaxSignal/fxcg20-Linux-bundle),
which builds the kernel ([MaxSignal/linux](https://github.com/MaxSignal/linux),
branch `fxcg20-7.2.y`) and the BusyBox system it starts.

## What it does

Its menu: **F1** boots `linux.bin` in RAM (below), **F2** the kernel that
runs in place from the flash (next section), **F6** removes that one;
**EXE** is F2 when there is something to boot or install there, F1
otherwise.

1. Opens `\\fls0\linux.bin` (a SuperH zImage) in the storage memory.
2. Stages it in whatever RAM the Casio OS leaves to an add-in: the free part
   of the 512 KiB add-in RAM (up to 32 KiB below the stack), blocks of the OS
   heap (`sys_malloc`), and finally the VRAM, which is the start of the
   physical RAM. Every page's physical address is read from the UTLB, so no
   assumption is made about how the OS maps the add-in RAM. The staged data
   is written back from the operand cache.
3. Copies `trampoline.S` and a table of the staged pieces to the on-chip IL
   memory (0xE5200000) and jumps there.
4. The trampoline, running from IL memory with SR.BL=1:
   - zeroes every INTC priority and masks every source, stops TMU0-2 and
     the watchdog,
   - turns the MMU off (MMUCR.TI, AT=0) and invalidates both caches,
   - gathers the pieces at physical 0x08040000 and jumps there (P0).
5. The zImage moves itself to its link address and decompresses the
   kernel to 0x08002000.

The add-in refuses to run when the add-in RAM is not in the first 2 MiB
of physical RAM (that is, on an fx-CG50).

There is no way back to the Casio OS except a reset: the kernel owns all
of the RAM. The loader never writes to the flash.

### The kernel in the flash

The bundle's `xip.bin` is a kernel and a romfs root file system that run
in place from the free flash after the Casio OS image, packed below
0xb40000 with a header in its last 256 bytes. F2 checks that header and
the CRCs of the kernel and the romfs, and the trampoline jumps straight
to the kernel (no pieces to gather).

When the flash does not hold it, or `\\fls0\xip.bin` is a different image
(its header differs), F2 boots `linux.bin` to install it, and F6 boots
`linux.bin` to remove it: the add-in cannot write the flash it and the
Casio OS run from, Linux running from RAM can (`/dev/xipflash`). The
loader tells the kernel which with a word at the end of the IL memory,
0xE5200FF0: `"fxcgMODE"`, then 1 (install, and start it) or 2 (remove);
0 for a plain boot.

### Hand-over state

| | |
|---|---|
| SR | MD=1, RB=0, BL=1, IMASK=15 |
| MMU | off, UTLB invalidated |
| caches | enabled as the OS left them, contents invalidated |
| INTC | all IPR fields 0, all IMR bits set |
| PC | 0x08040000 (start of the zImage), or the kernel's entry in the flash |
| boot parameters | none (the kernel uses its built-in command line) |

### Image size

On an fx-CG20 the three areas hold about 700 KiB. The image must also fit
below the end of RAM once gathered: `0x08040000 + size <= 0x08200000`.

## Building

Needs a big-endian SH-4A GCC for Linux (symbols without a leading
underscore), for instance the `sh4eb-linux-muslfdpic` toolchain built by
fxcg20-Linux-bundle, and Python 3:

```sh
make CROSS_COMPILE=sh4eb-linux-muslfdpic-
```

This gives `fxcglinux.g3a`. `mkg3a.py` wraps the raw binary into a `.g3a`
(a Python port of the header code of [mkg3a](https://github.com/tari/mkg3a)
by Peter Marheine; its output is byte-identical) with a generated icon.

## Files

| | |
|---|---|
| `crt0.S` | add-in start-up (`.data` copy, `.bss` clear), after libfxcg's crt0 |
| `main.c` | staging, UTLB translation, user interface |
| `syscalls.S` | the Casio OS syscalls used (via 0x80020070) |
| `trampoline.S` | hand-over code run from IL memory |
| `prizm.ld` | add-in memory layout (code 0x00300000, RAM 0x08100000) |
| `mkg3a.py` | `.g3a` packer |

## Licence

GPL-2.0 (`LICENSE`). `mkg3a.py` is derived from mkg3a (zlib licence).
