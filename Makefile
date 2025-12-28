CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c17 -O2

PREFIX = $(HOME)/.local
BINDIR = $(PREFIX)/bin

SRC = src/edit.c
TARGET = edit
TEST_SRC = test_utf8.c
TEST_TARGET = test_utf8

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(TEST_TARGET): $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)

install: $(TARGET)
	mkdir -p $(BINDIR)
	cp $(TARGET) $(BINDIR)/

uninstall:
	rm -f $(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall test
