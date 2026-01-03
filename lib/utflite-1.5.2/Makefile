CC = clang
CFLAGS = -Wall -Wextra -pedantic -std=c17 -O3
CFLAGS_DEBUG = -Wall -Wextra -pedantic -std=c17 -g -O0
AR = ar
ARFLAGS = rcs

PREFIX = /usr/local
INCLUDEDIR = $(PREFIX)/include
LIBDIR = $(PREFIX)/lib

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
TESTDIR = test
SINGLE_HEADER_DIR = single_include

# Files
SRC = $(SRCDIR)/utflite.c
OBJ = $(BUILDDIR)/utflite.o
LIB = $(BUILDDIR)/libutflite.a
HEADER = $(INCDIR)/utflite/utflite.h
SINGLE_HEADER = $(SINGLE_HEADER_DIR)/utflite.h

.PHONY: all clean install uninstall test test-single debug

all: $(LIB)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(OBJ): $(SRC) $(HEADER) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)
	rm -f $(TESTDIR)/test_utflite $(TESTDIR)/test_single

install: $(LIB) $(HEADER) $(SINGLE_HEADER)
	install -d $(INCLUDEDIR)/utflite
	install -d $(LIBDIR)
	install -m 644 $(HEADER) $(INCLUDEDIR)/utflite/utflite.h
	install -m 644 $(SINGLE_HEADER) $(INCLUDEDIR)/utflite_single.h
	install -m 644 $(LIB) $(LIBDIR)/

uninstall:
	rm -rf $(INCLUDEDIR)/utflite
	rm -f $(INCLUDEDIR)/utflite_single.h
	rm -f $(LIBDIR)/libutflite.a

# Test using static library
test: $(LIB) $(TESTDIR)/test_utflite.c
	$(CC) $(CFLAGS_DEBUG) -I$(INCDIR) $(TESTDIR)/test_utflite.c -L$(BUILDDIR) -lutflite -o $(TESTDIR)/test_utflite
	./$(TESTDIR)/test_utflite

# Test using single-header version
test-single: $(TESTDIR)/test_utflite.c $(SINGLE_HEADER)
	$(CC) $(CFLAGS_DEBUG) -DUTFLITE_SINGLE_HEADER -I$(SINGLE_HEADER_DIR) $(TESTDIR)/test_utflite.c -o $(TESTDIR)/test_single
	./$(TESTDIR)/test_single

# Debug build
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: clean $(LIB)
