EXT=crc32
VPATH=$(EXT)

CFLAGS=$(patsubst %,-I%,$(VPATH))
CFLAGS+=-Wextra -Wall -g -Os 

CFLAGS+=-DCFG_LUBI_DBG
CFLAGS+=-DCFG_LUBI_INT_CRC32

EXEC=main
OBJS=main.o crc32.o liblubi.o

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

all: $(EXEC)

clean:
	rm -f $(OBJS) $(EXEC)
