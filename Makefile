# fxcg-linux loader add-in (.g3a) for the Casio fx-CG10/fx-CG20
CROSS_COMPILE ?= sh4eb-linux-muslfdpic-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
PYTHON  ?= python3

CFLAGS  := -mb -m4a-nofpu -mhitachi -mno-fdpic -fno-pic -Os -Wall -Wextra \
	   -ffreestanding -fno-builtin -nostdlib -fno-strict-aliasing \
	   -fno-asynchronous-unwind-tables
LDFLAGS := -nostdlib -Wl,-T,prizm.ld -Wl,--no-warn-rwx-segments -Wl,-static

OBJS := crt0.o main.o syscalls.o trampoline.o

all: fxcglinux.g3a

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

fxcglinux.elf: $(OBJS) prizm.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

fxcglinux.bin: fxcglinux.elf
	$(OBJCOPY) -O binary -R .bss $< $@

fxcglinux.g3a: fxcglinux.bin mkg3a.py
	$(PYTHON) mkg3a.py --name Linux --version 00.01.0000 $< $@

clean:
	rm -f *.o *.elf *.bin *.g3a

.PHONY: all clean
