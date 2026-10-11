CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -Wall -Wextra -Wpedantic
LDLIBS  = -lm -lpthread
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=
SRC     = $(wildcard *.c)
LIBSRC  = $(filter-out main.c,$(SRC))

.PHONY: all test smoke check bench install uninstall clean

all: asmr

asmr: $(SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

test: tests/test_all.c $(LIBSRC) $(wildcard *.h)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -I. -fsanitize=address,undefined -o asmr_test tests/test_all.c $(LIBSRC) $(LDLIBS)
	./asmr_test

smoke: asmr
	@set -eu; ./asmr --help >/dev/null; ./asmr --version >/dev/null; ./asmr --list >/dev/null; if ./asmr --definitely-not-an-option >/dev/null 2>&1; then echo 'unknown option unexpectedly accepted' >&2; exit 1; fi; if ./asmr -f nope >/dev/null 2>&1; then echo 'invalid FPS unexpectedly accepted' >&2; exit 1; fi; if ./asmr -O /tmp/asmr-smoke.wav -d /tmp/asmr-smoke.raw >/dev/null 2>&1; then echo 'conflicting outputs unexpectedly accepted' >&2; exit 1; fi; ./asmr -n -F 3 -s rain -S 7 >/dev/null; ./asmr -F 3 -d /tmp/asmr-smoke.raw -s rain -S 7 >/dev/null; test "$$(wc -c < /tmp/asmr-smoke.raw | tr -d ' ')" -eq 17640; ./asmr -F 3 -O /tmp/asmr-smoke.wav -s rain -S 7 >/dev/null; test "$$(wc -c < /tmp/asmr-smoke.wav | tr -d ' ')" -eq 17684; rm -f /tmp/asmr-smoke.wav /tmp/asmr-smoke.raw; echo 'CLI and audio-export smoke tests passed.'

check: test smoke

bench: asmr
	./asmr -B 200

install: asmr
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 asmr "$(DESTDIR)$(BINDIR)/asciiasmr"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/asciiasmr"

clean:
	rm -f asmr asmr_test
