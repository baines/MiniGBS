SRC     := minigbs.c debug.c audio.c ui.c
CFLAGS  := -g -O0 -D_GNU_SOURCE -std=gnu99
LDFLAGS := -lncursesw -lSDL2 -lm -ltinfo

minigbs: $(SRC) minigbs.h
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

clean:
	$(RM) minigbs
