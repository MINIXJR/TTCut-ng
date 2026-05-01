# tools/ttcut-pts-analyze/Makefile
CC      = gcc
CFLAGS  = -pipe -std=c17 -Wall -Wextra -O2
TARGET  = ttcut-pts-analyze

$(TARGET): ttcut-pts-analyze.c
	$(CC) $(CFLAGS) -o $@ $<

install:
	install -m 755 $(TARGET) /usr/bin/

clean:
	rm -f $(TARGET)

.PHONY: install clean
