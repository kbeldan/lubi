-include config.mk

CFLAGS += $(patsubst %,-I%,$(VPATH)) -std=c99
CFLAGS += -Wextra -Wall -Werror -g -Os

ifdef ENABLE_DEBUG
CPPFLAGS += -DCFG_LUBI_DBG
endif

CPPFLAGS += -DCFG_LUBI_INT_CRC32

EXE = lubi
OBJS = main.o crc32.o liblubi.o
PROGRAMS = $(EXE)

ifdef ENABLE_TESTS
PROGRAMS += nandsim.sh
endif

all: $(EXE)

$(OBJS): config.h

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f $(OBJS) $(EXE)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PROGRAMS) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
