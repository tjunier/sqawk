.PHONY: all doc

CFLAGS := -std=c99 -Wall -Wextra -Werror -g
INSTALL_PREFIX := /usr/local
BIN_INSTALL_DIR = $(INSTALL_PREFIX)/bin
MAN_INSTALL_DIR = $(INSTALL_PREFIX)/share/man/man1

test: all
	./test_buffered_CSV
	./test_sqawk.sh

all: sqawk doc test_buffered_CSV

sqawk: sqawk.c buffered_CSV.c buffered_CSV.h
	$(CC) $(CFLAGS) -o $@ $< buffered_CSV.c -lsqlite3 -lm

test_buffered_CSV: buffered_CSV.c buffered_CSV.h
	$(CC) $(CFLAGS) -DTEST_BUFFERED_CSV -o $@ $< -lm

install: sqawk
	install sqawk $(BIN_INSTALL_DIR)
	install -d $(MAN_INSTALL_DIR)
	install sqawk.1 $(MAN_INSTALL_DIR)

doc:
	$(MAKE) --directory=$@

clean:
	$(RM) .*.sw? *.o sqawk *.db test*.{out,exp} test_buffered_CSV
