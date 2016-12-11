curmix: curmix.c
	cc $^ -o $@ -D_XOPEN_SOURCE_EXTENDED -lcurses -lpulse -Wall
