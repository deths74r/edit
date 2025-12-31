CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c17 -O2
LDFLAGS = -lm

PREFIX = $(HOME)/.local
BINDIR = $(PREFIX)/bin
THEMEDIR = $(HOME)/.edit/themes

SRC = src/edit.c src/error.c
TARGET = edit
TEST_SRC = test_utf8.c
TEST_TARGET = test_utf8

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)

install: $(TARGET)
	mkdir -p $(BINDIR)
	cp $(TARGET) $(BINDIR)/
	mkdir -p $(THEMEDIR)
	cp themes/*.ini $(THEMEDIR)/

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -rf $(THEMEDIR)

.PHONY: all clean install uninstall test
