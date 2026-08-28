CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -I../include

test_rfc3339: test_rfc3339.c ../rfc3339.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: run clean
run: test_rfc3339
	./test_rfc3339

clean:
	rm -f test_rfc3339
