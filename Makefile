PLATFORM   = m68k-unknown-elf
TOOLCHAIN  = /opt/Retro68-build/toolchain
HOSTSYSTEM = $(TOOLCHAIN)/bin/$(PLATFORM)
LIBDESTDIR = $(TOOLCHAIN)/$(PLATFORM)/lib

CC      = $(HOSTSYSTEM)-gcc
LD      = $(HOSTSYSTEM)-g++
AR      = $(HOSTSYSTEM)-ar rcs
CFLAGS  = -O3 -DNDEBUG -MMD
CFLAGS += -Wno-multichar
LDFLAGS = $(TOOLCHAIN)/../build-target/Console/libRetroConsole.a -lretrocrt
LDFLAGS+= -O3 -DNDEBUG -Wl,-elf2flt -Wl,-q -Wl,-Map=linkmap.txt -Wl,-undefined=consolewrite -Wl,-gc-sections

BIN     = StreamTest
SRC     = $(wildcard src/*.c)
INC     = $(wildcard src/*.h)
OBJ     = $(SRC:.c=.o)
DEP     = $(SRC:.c=.d)
SHAREDIR= Shared

LIB     = libcstreams.a
SRC_LIB = src/stream.c src/filestream.c src/tcpstream.c src/dnr.c
OBJ_LIB = $(SRC_LIB:.c=.o)
INC_LIB = $(SRC_LIB:.c=.h)

MINI_VMAC_DIR=~/Mac/Emulation/Mini\ vMac
MINI_VMAC=$(MINI_VMAC_DIR)/Mini\ vMac
MINI_VMAC_LAUNCHER_DISK=$(MINI_VMAC_DIR)/launcher-sys.dsk

ifndef V
	QUIET_CC   = @echo ' CC   ' $<;
	QUIET_LINK = @echo ' LINK ' $@;
	QUIET_APPL = @echo ' APPL ' $*;
	QUIET_RUN  = @echo ' RUN  ' $<;
	QUIET_AR   = @echo ' AR   ' $@;
endif

all: $(LIB) $(BIN).bin

-include $(DEP)

%.dsk %.bin %.APPL .rsrc/%.APPL .finf/%.APPL: %
	$(QUIET_APPL)$(TOOLCHAIN)/bin/MakeAPPL -c $< -o $*

$(BIN): $(OBJ)
	$(QUIET_LINK)$(LD) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

check: $(SRC) $(INC)
	@awk -f stylecheck.awk $? && touch $@

wc:
	@wc -l $(SRC) $(INC) | sort -n

$(LIB): $(OBJ_LIB)
	$(QUIET_AR)$(AR) $@ $^

install: $(LIB)
	cp $(LIB) $(LIBDESTDIR)

clean:
	rm -f $(BIN) $(OBJ) $(DEP) check linkmap.txt \
		$(BIN).dsk $(BIN).bin $(BIN).68k $(BIN).gdb

run: $(BIN).dsk
	$(QUIET_RUN)$(MINI_VMAC) $(MINI_VMAC_LAUNCHER_DISK) $(DISK) $(BIN).dsk

share: $(BIN).APPL
	cp $(BIN).APPL $(SHAREDIR)/
	@cp .rsrc/$(BIN).APPL $(SHAREDIR)/.rsrc/
	@cp .finf/$(BIN).APPL $(SHAREDIR)/.finf/

.PHONY: clean wc
