CC     := clang
CFLAGS := -std=c17 -g -Werror -Wall -Wextra -Weverything -pedantic
CFLAGS += -Wno-shadow -Wno-declaration-after-statement

kilo: kilo.c
	$(CC) $(CFLAGS) -o kilo $<

check:
	$(CC) $(CFLAGS) -fsyntax-only kilo.c

run: kilo
	./kilo

clean:
	-rm -f kilo
