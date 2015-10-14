CFLAGS+=-std=gnu99 -g -Wall -Wextra -pedantic -O2 -funroll-loops -ftree-vectorize
LIBS=-lm
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MAN1DIR=$(PREFIX)/share/man/man1

SRCS=ioping.c
OBJS=$(SRCS:.c=.o)
BINS=ioping
MANS=ioping.1
MANS_F=$(MANS:.1=.txt) $(MANS:.1=.pdf)
DOCS=README.md LICENSE changelog
SPEC=ioping.spec

PACKAGE=ioping
EXTRA_VERSION:=$(shell test -d .git && git describe --tags --dirty=+ | sed 's/^v[^-]*//;s/-/./g')
VERSION:=$(shell sed -ne 's/\# define VERSION \"\(.*\)\"/\1/p' ioping.c)${EXTRA_VERSION}
DISTDIR=$(PACKAGE)-$(VERSION)
DISTFILES=$(SRCS) $(MANS) $(DOCS) $(SPEC) Makefile
PACKFILES=$(BINS) $(MANS) $(MANS_F) $(DOCS)

STRIP=strip
TARGET=$(shell ${CC} -dumpmachine | cut -d- -f 2)

ifdef MINGW
CC=i686-w64-mingw32-gcc
STRIP=i686-w64-mingw32-strip
TARGET=win32
BINS:=$(BINS:=.exe)
endif

all: $(BINS)

version:
	@echo ${VERSION}

clean:
	$(RM) -f $(OBJS) $(BINS) $(MANS_F)

strip: $(BINS)
	$(STRIP) $^

install: $(BINS) $(MANS)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -s -m 0755 $(BINS) $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MAN1DIR)
	install -m 644 $(MANS) $(DESTDIR)$(MAN1DIR)

%.o: %.c
	$(CC) $(CFLAGS) -DEXTRA_VERSION=\"${EXTRA_VERSION}\" -c -o $@ $<

%.ps: %.1
	man -t ./$< > $@

%.pdf: %.ps
	ps2pdf $< $@

%.txt: %.1
	MANWIDTH=80 man ./$< | col -b > $@

$(BINS): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

dist: $(DISTFILES)
	tar -cz --transform='s,^,$(DISTDIR)/,S' $^ -f $(DISTDIR).tar.gz

binary-tgz: $(PACKFILES)
	${STRIP} ${BINS}
	tar -cz --transform='s,^,$(DISTDIR)/,S' -f ${PACKAGE}-${VERSION}-${TARGET}.tgz $^

binary-zip: $(PACKFILES)
	${STRIP} ${BINS}
	ln -s . $(DISTDIR)
	zip ${PACKAGE}-${VERSION}-${TARGET}.zip $(addprefix $(DISTDIR)/,$^)
	rm $(DISTDIR)

.PHONY: all clean install dist version
