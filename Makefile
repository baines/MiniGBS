minigbs: minigbs.c debug.c audio.c minigbs.h
	gcc -g -O2 -std=gnu99 minigbs.c debug.c audio.c -o $@ -lncursesw -lSDL2 -lm -ltinfo
