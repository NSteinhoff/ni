CC     := clang
CFLAGS := -std=c17 -g -Werror -Wall -Wextra -Weverything -pedantic
CFLAGS += -Wno-shadow -Wno-declaration-after-statement -Wno-padded -Wno-unsafe-buffer-usage
MAX_LINES := 1024

ni: ni.c
	@wc -l ni.c | (read n _; \
		echo Lines: $$n; \
		[ "$$n" -lt ${MAX_LINES} ] \
		|| (echo "Too many lines. Maximum number of lines is ${MAX_LINES}" && false) \
	)
	$(CC) $(CFLAGS) -o ni $<

check:
	$(CC) $(CFLAGS) -fsyntax-only ni.c
.PHONY: check

install: ni
	@ln -s $$(pwd)/ni ~/.local/bin/ni
.PHONY: install

uninstall:
	@rm -f ~/.local/bin/ni
.PHONY: uninstall

clean:
	-rm -f ni
.PHONY: clean
