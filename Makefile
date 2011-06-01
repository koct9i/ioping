CFLAGS=-std=c99 -g -Wall -Wextra -pedantic -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS=
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

all: ioping

clean:
	$(RM) -f ioping.o ioping

install: ioping
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 ioping $(DESTDIR)$(BINDIR)

ioping.o: ioping.c
	$(CC) -c -o $@ $^ ${CFLAGS}

ioping: ioping.o
	$(CC) -o $@ $^ -lm ${LDFLAGS}

.PHONY: all clean install
