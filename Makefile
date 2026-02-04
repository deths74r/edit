# Makefile for edit - Terminal Text Editor
# C23 compliant build

CC = gcc
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")
CFLAGS = -std=c23 -Wall -Wextra -Wpedantic -D_DEFAULT_SOURCE -D_GNU_SOURCE -I lib/gstr/include -DEDIT_VERSION=\"$(VERSION)\"
LDFLAGS =

# Build type (debug or release)
BUILD ?= debug

ifeq ($(BUILD),debug)
    CFLAGS += -g -O0 -DDEBUG
else ifeq ($(BUILD),release)
    CFLAGS += -O2 -DNDEBUG
endif

TARGET = edit
SRC = edit.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean debug release lint test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug:
	$(MAKE) BUILD=debug

release:
	$(MAKE) BUILD=release

lint:
	@echo "Checking for stray control characters..."
	@if grep -Pn '[\x00-\x08\x0b-\x0c\x0e-\x1f\x7f]' $(SRC); then \
		echo "FAIL: stray control characters found"; exit 1; \
	else \
		echo "OK: no stray bytes"; \
	fi

test: test_edit
	./test_edit

test_edit: test_edit.c edit.c lib/gstr/include/gstr.h
	$(CC) $(CFLAGS) -o test_edit test_edit.c

clean:
	rm -f $(TARGET) $(OBJ) test_edit
