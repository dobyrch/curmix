pamixer: pamixer.c
	clang $^ -o $@ -g -O0 -D_XOPEN_SOURCE_EXTENDED -lpulse -lcurses
