CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c11

PREFIX = $(HOME)/.local
BINDIR = $(PREFIX)/bin

SRC = src/edit.c
TARGET = edit

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

install: $(TARGET)
	mkdir -p $(BINDIR)
	cp $(TARGET) $(BINDIR)/

uninstall:
	rm -f $(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
