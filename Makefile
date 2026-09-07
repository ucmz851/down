UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    CC ?= clang
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || ( [ -d /opt/homebrew ] && echo /opt/homebrew ) || echo /usr/local)
    OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo $(BREW_PREFIX)/opt/openssl)
    CURL_PREFIX := $(shell brew --prefix curl 2>/dev/null || echo $(BREW_PREFIX)/opt/curl)
    export PKG_CONFIG_PATH := $(OPENSSL_PREFIX)/lib/pkgconfig:$(CURL_PREFIX)/lib/pkgconfig:$(BREW_PREFIX)/lib/pkgconfig:$(PKG_CONFIG_PATH)
    MAC_CFLAGS := -I$(BREW_PREFIX)/include -I$(OPENSSL_PREFIX)/include -I$(CURL_PREFIX)/include
    MAC_LDFLAGS := -L$(BREW_PREFIX)/lib -L$(OPENSSL_PREFIX)/lib -L$(CURL_PREFIX)/lib
else
    CC ?= gcc
    MAC_CFLAGS :=
    MAC_LDFLAGS :=
endif

CFLAGS ?= -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude
CFLAGS += $(MAC_CFLAGS)

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
CRYPTO_CFLAGS := $(shell pkg-config --cflags libcrypto 2>/dev/null)
CRYPTO_LIBS := $(shell pkg-config --libs libcrypto 2>/dev/null || echo -lcrypto)
LIBS := $(MAC_LDFLAGS) $(CURL_LIBS) $(CRYPTO_LIBS) -lpthread -lm

SRCDIR := src
INCDIR := include
BUILDDIR := build
BIN := down

SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

PREFIX ?= /usr/local

.PHONY: all clean test install uninstall

all: $(BIN)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CURL_CFLAGS) $(CRYPTO_CFLAGS) -c $< -o $@

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

test: all
	@chmod +x tests/run_tests.sh
	@tests/run_tests.sh

clean:
	rm -rf $(BUILDDIR) $(BIN) inlay test_* *.down *.inlay *.out *.tmp

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN) $(DESTDIR)$(PREFIX)/bin/inlay
