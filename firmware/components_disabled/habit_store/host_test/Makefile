CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -I../include

test_streak: test_streak.c ../streak_engine.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: run clean
run: test_streak
	./test_streak

clean:
	rm -f test_streak
