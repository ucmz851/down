CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic -O3 -std=gnu11 -D_GNU_SOURCE -Iinclude
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
CRYPTO_CFLAGS := $(shell pkg-config --cflags libcrypto)
CRYPTO_LIBS := $(shell pkg-config --libs libcrypto || echo -lcrypto)
LIBS := $(CURL_LIBS) $(CRYPTO_LIBS) -lpthread -lm

SRCDIR := src
INCDIR := include
BUILDDIR := build
BIN := inlay

SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

.PHONY: all clean test install

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
	rm -rf $(BUILDDIR) $(BIN) test_* *.inlay *.out *.tmp

install: $(BIN)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/local/bin/$(BIN)
