CC = cc
CPPFLAGS = -Iinclude
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS =
SOURCES = src/main.c src/cli.c src/matching_engine.c src/order_book.c src/price_tree.c src/input.c src/csv_manager.c
HEADERS = $(wildcard include/*.h src/*.h)

.PHONY: all clean

all: matching-engine

matching-engine: $(SOURCES) $(HEADERS) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

clean:
	rm -f matching-engine
