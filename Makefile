CC     := clang
CFLAGS := -std=c17 -g -Werror -Wall -Wextra -Weverything -pedantic
CFLAGS += -Wno-shadow -Wno-declaration-after-statement -Wno-padded -Wno-unsafe-buffer-usage
MAX_LINES := 1000

ni: term.c ni.o
	@cat ni.c term.c | grep -hv -e '^$$' -e '^//' | wc -l | (read n _; \
		echo Lines: $$n; \
		[ "$$n" -lt ${MAX_LINES} ] \
		|| (echo "Too many lines. Maximum number of lines is ${MAX_LINES}" && false) \
	)
	$(CC) $(CFLAGS) -o ni $^

ni.o: ni.c ni.h
	$(CC) $(CFLAGS) -c ni.c

check:
	$(CC) $(CFLAGS) -fsyntax-only term.c ni.c
.PHONY: check

install: ni
	@ln -s $$(pwd)/ni ~/.local/bin/ni
.PHONY: install

uninstall:
	@rm -f ~/.local/bin/ni
.PHONY: uninstall

clean:
	-rm -f ni ni.o
.PHONY: clean

leaks:
	@leaks -atExit -quiet -readonlyContent -- ni test.txt
.PHONY: leaks
