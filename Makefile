CFLAGS ?= -g -O2 -funroll-loops -ftree-vectorize
CFLAGS += -std=gnu99 -Wall -Wextra -pedantic
LIBS=-lm -lrt
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MAN1DIR=$(PREFIX)/share/man/man1

SRCS=ioping.c
OBJS=$(SRCS:.c=.o)
BINARY=ioping
MANS=ioping.1
MANS_F=$(MANS:.1=.txt) $(MANS:.1=.pdf)
DOCS=README.md LICENSE changelog
SPEC=ioping.spec

PACKAGE=ioping
GIT_VER:=$(shell test -d .git && git describe --tags --match 'v[0-9]*' \
		--abbrev=0 | sed 's/v//')
SRC_VER:=$(shell sed -ne 's/\# define VERSION \"\(.*\)\"/\1/p' ioping.c)
EXTRA_VERSION:=$(shell test -d .git && git describe --tags --match 'v[0-9]*' \
		--dirty=+ | sed 's/^v[^-]*//;s/-/./g')
VERSION:=$(SRC_VER)$(EXTRA_VERSION)
DISTDIR=$(PACKAGE)-$(VERSION)
DISTFILES=$(SRCS) $(MANS) $(DOCS) $(SPEC) Makefile
PACKFILES=$(BINARY) $(MANS) $(MANS_F) $(DOCS)
CPPFLAGS+=-DEXTRA_VERSION=\"${EXTRA_VERSION}\"

STRIP=strip
TARGET=$(shell ${CC} -dumpmachine)

ifneq (,$(findstring -apple-,${TARGET}))
LIBS=-lm
endif

ifdef MINGW
CC=i686-w64-mingw32-gcc
STRIP=i686-w64-mingw32-strip
TARGET=win32
BINARY:=$(BINARY:=.exe)
LIBS=-lm
endif

all: checkver $(BINARY)

version: checkver
	@echo ${VERSION}

checkver:
	@if test -n "$(GIT_VER)" -a "$(GIT_VER)" != "$(SRC_VER)"; then \
		echo "ERROR: Version mismatch between git and source"; \
		echo git: $(GIT_VER), src: $(SRC_VER); \
		exit 1; \
	fi

clean:
	$(RM) -f $(OBJS) $(BINARY) $(MANS_F) ioping.tmp

strip: $(BINARY)
	$(STRIP) $^

test: $(BINARY)
	./$(BINARY) -i 10ms -c 3 -s 512 -S 16k ${PWD}
	./$(BINARY) -w 10ms -R -k -S 1m .
	./$(BINARY) -w 10ms -RL ioping.tmp
	rm ioping.tmp

install: $(BINARY) $(MANS)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(BINARY) $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MAN1DIR)
	install -m 644 $(MANS) $(DESTDIR)$(MAN1DIR)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.ps: %.1
	man -t ./$< > $@

%.pdf: %.ps
	ps2pdf $< $@

%.txt: %.1
	MANWIDTH=80 man ./$< | col -b > $@

$(BINARY): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

dist: checkver $(DISTFILES)
	tar -cz --transform='s,^,$(DISTDIR)/,S' $^ -f $(DISTDIR).tar.gz

binary-tgz: checkver $(PACKFILES)
	${STRIP} ${BINARY}
	tar -cz --transform='s,^,$(DISTDIR)/,S' -f ${PACKAGE}-${VERSION}-${TARGET}.tgz $^

binary-zip: checkver $(PACKFILES)
	${STRIP} ${BINARY}
	ln -s . $(DISTDIR)
	zip ${PACKAGE}-${VERSION}-${TARGET}.zip $(addprefix $(DISTDIR)/,$^)
	rm $(DISTDIR)

.PHONY: all version checkver clean strip test install dist binary-tgz binary-zip
