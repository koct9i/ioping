CFLAGS+=-std=c99 -g -Wall -Wextra -pedantic -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS=-lm
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MAN1DIR=$(PREFIX)/share/man/man1

SRCS=ioping.c
OBJS=$(SRCS:.c=.o)
BINS=ioping
MANS=ioping.1
SPEC=ioping.spec

PACKAGE=ioping
VERSION=$(shell cat version)
DISTDIR=$(PACKAGE)-$(VERSION)
DISTFILES=$(SRCS) $(MANS) $(SPEC) Makefile

all: version $(BINS)

version: $(DISTFILES)
	test ! -d .git || git describe --tags --dirty=+ | sed 's/^v//;s/-/./g' > $@

clean:
	$(RM) -f $(OBJS) $(BINS)

install: $(BINS) $(MANS)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(BINS) $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MAN1DIR)
	install -m 644 $(MANS) $(DESTDIR)$(MAN1DIR)

%.o: %.c
	$(CC) $(CFLAGS) -DVERSION=\"${VERSION}\" -c -o $@ $^

ioping: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

dist: version $(DISTFILES)
	tar -cz --transform='s,^,$(DISTDIR)/,S' $^ -f $(DISTDIR).tar.gz

.PHONY: all clean install dist
