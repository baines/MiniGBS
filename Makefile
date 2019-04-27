SRC     := minigbs.c debug.c audio.c ui.c x11.c radio.c
CFLAGS  := -g
LDFLAGS := -lncursesw -ltinfo -lm -lasound -ldl
INSTALL := install -D
prefix  := /usr/local

all: minigbs gbsradio

minigbs: $(SRC) minigbs.h
	$(CC) $(SRC) -D_GNU_SOURCE -std=gnu99 $(CFLAGS) -o $@ $(LDFLAGS)

gbsradio: gbsradio.c
	$(CC) $^ -D_GNU_SOURCE -std=gnu99 $(CFLAGS) -o $@ -lncursesw

install: minigbs
	$(INSTALL) $< $(DESTDIR)$(prefix)/bin/minigbs

clean:
	$(RM) minigbs

.PHONY: install clean all
