# fxcg-linux-loader

A Casio fx-CG10 / fx-CG20 add-in that boots Linux. It is the loader of
[fxcg20-Linux-bundle](https://github.com/MaxSignal/fxcg20-Linux-bundle).

| Key | |
|---|---|
| F1 | Boot `\\fls0\linux.bin` |
| F2 | Boot `\\fls0\linuxf.bin` when its flash part is installed; otherwise boot `linux.bin` to install `\\fls0\flash.bin` |
| F6 | Boot `linux.bin` to remove the flash part |
| EXE | F2 if available, otherwise F1 |

The kernel file is loaded into the RAM the Casio OS leaves to an
add-in, then `trampoline.S`, run from the on-chip IL memory, turns the
MMU and interrupts off, gathers the image at 0x08040000 and jumps to it.
The word at 0xE5200FF0 (`"fxcgMODE"`, then 0 boot, 1 install, 2 remove)
tells the kernel what to do.

## Building

Needs a big-endian SH-4A GCC for Linux, for instance the
`sh4eb-linux-muslfdpic` toolchain built by fxcg20-Linux-bundle, and
Python 3:

```sh
make CROSS_COMPILE=sh4eb-linux-muslfdpic-
```

This gives `fxcglinux.g3a`. `mkg3a.py` is a Python port of the header
code of [mkg3a](https://github.com/tari/mkg3a) by Peter Marheine.

## Licence

GPL-2.0 (`LICENSE`). `mkg3a.py` is derived from mkg3a (zlib licence).
