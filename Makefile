SRC     := minigbs.c debug.c audio.c audio_output.c wav_write.c ui.c x11.c
CFLAGS  := -g
LDFLAGS := -lncursesw -ltinfo -lm -lasound -ldl
INSTALL := install -D
prefix  := /usr/local

minigbs: $(SRC) minigbs.h
	$(CC) $(SRC) -D_GNU_SOURCE -std=gnu99 $(CFLAGS) -o $@ $(LDFLAGS)

install: minigbs
	$(INSTALL) $< $(DESTDIR)$(prefix)/bin/minigbs

clean:
	$(RM) minigbs

.PHONY: install clean
