# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>

CC = clang
CFLAGS = -Wall -Wextra -pedantic -std=c17 -O3
CFLAGS_DEBUG = -Wall -Wextra -pedantic -std=c17 -g -O0

PREFIX = /usr/local
INCLUDEDIR = $(PREFIX)/include

INCDIR = include
TESTDIR = test
HEADER = $(INCDIR)/gstr.h

# ============================================================================
# Git-based versioning
# ============================================================================
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "nogit")
GIT_TAG := $(shell git describe --tags --exact-match HEAD 2>/dev/null)
GIT_BRANCH := $(shell git branch --show-current 2>/dev/null)
LAST_TAG := $(shell git describe --tags --abbrev=0 2>/dev/null || echo "v0.0.0")
COMMITS_SINCE := $(shell git rev-list $(LAST_TAG)..HEAD --count 2>/dev/null || echo "0")

ifneq ($(GIT_TAG),)
    VERSION := $(patsubst v%,%,$(GIT_TAG))
else ifeq ($(GIT_BRANCH),main)
    VERSION := $(patsubst v%,%,$(LAST_TAG))+dev.$(COMMITS_SINCE)
else ifneq ($(GIT_BRANCH),)
    BRANCH_SUFFIX := $(shell echo "$(GIT_BRANCH)" | tr '/' '-' | cut -c1-30)
    VERSION := $(patsubst v%,%,$(LAST_TAG))+$(BRANCH_SUFFIX)
else
    VERSION := 0.0.0+unknown
endif

BUILD_TIME := $(shell date +%Y%m%d-%H%M%S)
BUILD_ID := $(GIT_HASH)-$(BUILD_TIME)
VERSION_FLAGS = -DGSTR_VERSION=\"$(VERSION)\" -DGSTR_BUILD_ID=\"$(BUILD_ID)\"

.PHONY: all clean install uninstall test version

all: test

# ============================================================================
# Test
# ============================================================================
test: $(TESTDIR)/test_gstr.c $(HEADER)
	$(CC) $(CFLAGS_DEBUG) $(VERSION_FLAGS) -I$(INCDIR) $< -o $(TESTDIR)/test_gstr
	./$(TESTDIR)/test_gstr

# ============================================================================
# Install/Uninstall (header-only library)
# ============================================================================
install: $(HEADER)
	install -d $(INCLUDEDIR)
	install -m 644 $(HEADER) $(INCLUDEDIR)/gstr.h

uninstall:
	rm -f $(INCLUDEDIR)/gstr.h

# ============================================================================
# Clean
# ============================================================================
clean:
	rm -f $(TESTDIR)/test_gstr

# ============================================================================
# Version info
# ============================================================================
version:
	@echo "Version: $(VERSION)"
	@echo "Build ID: $(BUILD_ID)"
