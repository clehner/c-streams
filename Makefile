TOOLCHAIN = /opt/Retro68-build/toolchain
HOSTSYSTEM = $(TOOLCHAIN)/bin/m68k-unknown-elf

BIN     = StreamTest
CC      = $(HOSTSYSTEM)-gcc
LD      = $(HOSTSYSTEM)-g++
SRC     = $(wildcard src/*.c)
INC     = $(wildcard src/*.h)
OBJ     = $(SRC:.c=.o)
DEP     = $(SRC:.c=.d)
CFLAGS  = -O3 -DNDEBUG
CFLAGS += -Wno-multichar -Wno-attributes
CFLAGS += -MMD -I$(TOOLCHAIN)/$(ARCH)/include
LDFLAGS = $(TOOLCHAIN)/../build-target/App2/libRetroConsole.a -lretrocrt
LDFLAGS+= -O3 -DNDEBUG -Wl,-elf2flt -Wl,-q -Wl,-Map=linkmap.txt -Wl,-undefined=consolewrite

MINI_VMAC_DIR=~/Mac/Emulation/Mini\ vMac
MINI_VMAC=$(MINI_VMAC_DIR)/Mini\ vMac
MINI_VMAC_LAUNCHER_DISK=$(MINI_VMAC_DIR)/launcher-sys.dsk

ifndef V
	QUIET_CC   = @echo ' CC   ' $<;
	QUIET_LINK = @echo ' LINK ' $@;
	QUIET_APPL = @echo ' APPL ' $@;
	QUIET_RUN  = @echo ' RUN  ' $<;
endif

all: $(BIN).bin

-include $(DEP)

%.dsk %.bin: %
	$(QUIET_APPL)$(TOOLCHAIN)/bin/MakeAPPL -c $< -o $*

$(BIN): $(OBJ)
	$(QUIET_LINK)$(LD) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

check: $(SRC) $(INC)
	@awk -f stylecheck.awk $? && touch $@

wc:
	@wc -l $(SRC) $(INC) | sort -n

clean:
	rm -f $(BIN) $(OBJ) $(DEP) check linkmap.txt \
		$(BIN).dsk $(BIN).bin $(BIN).68k $(BIN).gdb

run: $(BIN).dsk
	$(QUIET_RUN)$(MINI_VMAC) $(MINI_VMAC_LAUNCHER_DISK) $(DISK) $(BIN).dsk

.PHONY: clean wc
