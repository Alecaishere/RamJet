#
# RamJet - Rice clone in C
# Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
#

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS ?=
LDLIBS   = -lm

# Source files
SRCDIR   = src
SOURCES  = $(SRCDIR)/main.c \
           $(SRCDIR)/ramjet.c \
           $(SRCDIR)/rule.c \
           $(SRCDIR)/cgroup.c \
           $(SRCDIR)/proc_type.c \
           $(SRCDIR)/parse.c \
           $(SRCDIR)/class.c \
           $(SRCDIR)/cJSON.c

OBJECTS  = $(SOURCES:.c=.o)
TARGET   = ramjet

# Install paths
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

# Debug build
debug: CFLAGS += -g -DDEBUG -O0
debug: clean all

# Static analysis
analyze:
	clang-tidy $(SOURCES) -- $(CFLAGS)
