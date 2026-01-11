CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c17 -O2
LDFLAGS = -lm -pthread

PREFIX = $(HOME)/.local
BINDIR = $(PREFIX)/bin
THEMEDIR = $(HOME)/.edit/themes

SRC = src/edit.c src/error.c src/terminal.c src/theme.c src/buffer.c src/syntax.c src/undo.c src/input.c src/render.c src/worker.c src/search.c src/autosave.c src/dialog.c src/clipboard.c src/editor.c src/update.c src/keybindings.c src/main.c
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
