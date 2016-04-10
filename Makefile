curmix: curmix.c
	clang $^ -o $@ -D_XOPEN_SOURCE_EXTENDED -lcurses -lpulse  -Wall
