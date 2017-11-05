#include "minigbs.h"

#define GRID_W 60
#define GRID_H 60

static uint8_t grid[GRID_W][GRID_H];
static uint8_t (*col)[GRID_H] = &grid[0];
static int boldness[16*3];
static int msg_timer;
static char msg[128];

void ui_chart_set(uint16_t notes[static 3]){
	if(++col >= &grid[0] + GRID_W){
		col = &grid[0];
	}

	memset(col, 0, sizeof(*col));

	for(int i = 0; i < 3; ++i){
		int n = notes[i] - 5;
		if(n < 0 || n >= GRID_H) continue;
		
		(*col)[n] |= (1 << i);
	}
}

static void ui_chart_draw(void){
	static const char* glyphs[3] = {
		" ", "▀", "▄",
	};

	const int c = col - &grid[0];
	const int start_x = MAX(0, (cfg.win_w - GRID_W)/2);

	for(size_t x = 0; x < GRID_W; ++x){
		for(size_t y = 0; y < GRID_H; y += 2){
			int xx = (GRID_W + (c - x)) % GRID_W;

			int color_lo = __builtin_ffs(grid[xx][y+0]);
			int color_hi = __builtin_ffs(grid[xx][y+1]);

			int color;
			const char* glyph;

			if(color_hi && color_lo){
				glyph = glyphs[1 + (color_hi > color_lo)];
				color = color_hi + color_lo + 5;
			} else {
				glyph = glyphs[((color_lo > 0) << 1) | (color_hi > 0)];
				color = color_hi ?: color_lo;
			}

			attron(COLOR_PAIR(color));
			mvprintw((GRID_H/2) - (y/2), start_x + (GRID_W - x), "%s", glyph);
			attroff(COLOR_PAIR(color));
		}
	}
}

void ui_msg_set(const char* fmt, ...){
	va_list va;
	va_start(va, fmt);

	vsnprintf(msg, sizeof(msg), fmt, va);

	msg_timer = 20;
	va_end(va);
}

static void ui_msg_draw(void){
	if(msg_timer > 0){
		int x, y;
		getmaxyx(stdscr, y, x);
		move(y-1, 0);

		if(--msg_timer == 0){
			clrtoeol();
		} else {
			if(msg_timer > 15) attron(A_BOLD);
			printw("%s\n", msg);
			if(msg_timer > 15) attroff(A_BOLD);
		}
	}
}

static void ui_info_draw(struct GBSHeader* h){
	char buf[256] = {};
	int len = 0;

	move(0, 0);
	attron(A_BOLD);

	len = snprintf(buf, countof(buf), "[%d/%d]", cfg.song_no, h->song_count - 1);
	mvaddstr(0, cfg.win_w-len, buf);
	len = 0;
	*buf = 0;

	if(*h->title && strncmp(h->title, "<?>", 4) != 0){
		len += snprintf(buf + len, countof(buf) - len, "%.32s", h->title);
	}
	if(*h->copyright && strncmp(h->copyright, "<?>", 4) != 0){
		len += snprintf(buf + len, countof(buf) - len, "  ©%.32s", h->copyright);
	}
	mvaddstr(1, (cfg.win_w-len)/2, buf);
	*buf = 0;

	if(*h->author && strncmp(h->author, "<?>", 4) != 0){
		len = snprintf(buf, countof(buf), "by %.32s ", h->author);
	}
	mvaddstr(2, (cfg.win_w-len)/2, buf);

	attroff(A_BOLD);
}

void ui_regs_set(uint16_t addr, int val){
	boldness[addr - 0xFF10] = val;
}

static void ui_regs_draw(void){
	static const int color_map[3][16] = {
		{ 1, 1, 1, 1, 1, 5, 2, 2, 2, 2, 3, 3, 3, 3, 3, 5 },
		{ 4, 4, 4, 4, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
		{ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
	};

	int x = (cfg.win_w-47)/2;
	int y = (cfg.win_h-6)/6+4;

	move(y, x);
	attron(A_BOLD);
	for(int i = 0; i < 0x10; ++i) printw("  %1X", i);
	attroff(A_BOLD);

	for(int i = 0; i < 3; ++i){
		move(y+i+1, x-4);

		attron(A_BOLD);
		printw("FF%d ", i+1);
		attroff(A_BOLD);

		for(int j = 0; j < 16; ++j){
			if(boldness[i*16+j]){
				attron(A_BOLD);
			}

			attron(COLOR_PAIR(color_map[i][j]));
			printw(" %02x", mem[0xFF10 + (i*0x10) + j]);
			attroff(COLOR_PAIR(color_map[i][j]));

			if(boldness[i*16+j]){
				attroff(A_BOLD);
				--boldness[i*16+j];
			}
		}
	}
}

void ui_init(void){
	if(cfg.hide_ui) return;

	initscr();
	noecho();
	cbreak();
	timeout(0);
	curs_set(0);
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);
	set_escdelay(0);

	if(!cfg.monochrome){
		start_color();

		if(can_change_color() && COLORS > 8){
			init_color(COLOR_BLACK, 91, 91, 102);
		}

		init_pair(1, COLOR_CYAN   , COLOR_BLACK);
		init_pair(2, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(3, COLOR_RED    , COLOR_BLACK);
		init_pair(4, COLOR_YELLOW , COLOR_BLACK);
		init_pair(5, COLOR_BLACK  , COLOR_BLACK);
		init_pair(6, COLOR_GREEN  , COLOR_BLACK);
		init_pair(7, COLOR_WHITE  , COLOR_BLACK);

		init_pair(8 , COLOR_CYAN    , COLOR_MAGENTA);
		init_pair(9 , COLOR_CYAN    , COLOR_RED);
		init_pair(10, COLOR_MAGENTA , COLOR_RED);

		init_pair(11, COLOR_BLACK, COLOR_CYAN);
		init_pair(12, COLOR_BLACK, COLOR_MAGENTA);
		init_pair(13, COLOR_BLACK, COLOR_RED);
		init_pair(14, COLOR_BLACK, COLOR_YELLOW);

		bkgd(COLOR_PAIR(7));
	}

	getmaxyx(stdscr, cfg.win_h, cfg.win_w);
}

static void ui_notes_draw(uint16_t notes[static 4]){
	static const char* note_strs[] = {
		"A-", "A#", "B-", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#"
	};

	move((cfg.win_h-6)/6+9, (cfg.win_w-47)/2+9);
	attron(A_BOLD);

	for(int i = 0; i < 3; ++i){
		if(notes[i] != 0xffff){
			printw("[ ");
			attron(COLOR_PAIR(i+1));
			int octave = MAX(0, MIN(notes[i] / 12, 9));
			printw("%s%d", note_strs[notes[i] % 12], octave);
			attroff(COLOR_PAIR(i+1));
			printw(" ]     ");
		} else {
			printw("[     ]     ");
		}
	}
	attroff(A_BOLD);
}

static void ui_volume_draw(uint16_t notes[static 4]){
	static const char* vol_glyphs[] = {
		" ", "▎", "▌", "▊", "█",
	};

	static uint8_t prev_vol[8];
	uint8_t vol[8];
	audio_get_vol(vol);

	for(int i = 0; i < 8; ++i){
		if(notes[i/2] == 0xffff) vol[i] = 0;
		if(!prev_vol[i]) continue;

		vol[i] = MAX(prev_vol[i] - 1, vol[i]);
	}

	for(int i = 0; i < 4; ++i){
		move((cfg.win_h-6)/6+9, (cfg.win_w-47)/2+3+(12*i));

		attron(COLOR_PAIR(i+11));
		for(int j = 3; j --> 0 ;) {
			uint8_t v = vol[i*2+0] / 5;
			if(v == j){
				printw("%s", vol_glyphs[4-(vol[i*2+0] % 5)]);
			} else if(v > j){
				printw("%s", vol_glyphs[0]);
			} else {
				printw("%s", vol_glyphs[4]);
			}
		}
		attroff(COLOR_PAIR(i+11));

		attron(COLOR_PAIR(i+1));
		if(vol[i*2+0] || vol[i*2+1]) printw("█");

		for(int j = 0; j < 3; ++j){
			uint8_t v = vol[i*2+1] / 5;
			if(v == j){
				printw("%s", vol_glyphs[vol[i*2+1] % 5]);
			} else if(v > j){
				printw("%s", vol_glyphs[4]);
			} else {
				printw("%s", vol_glyphs[0]);
			}
		}

		attroff(COLOR_PAIR(i+1));
	}

	memcpy(prev_vol, vol, sizeof(vol));
}

void ui_redraw(struct GBSHeader* h){
	if(cfg.hide_ui) return;

	uint16_t notes[4] = {};
	audio_get_notes(notes);
	ui_chart_set(notes);

	if(cfg.ui_mode == UI_MODE_CHART){
		ui_chart_draw();
	} else {
		ui_info_draw(h);
		ui_regs_draw();

		if(cfg.ui_mode == UI_MODE_REGISTERS){
			ui_notes_draw(notes);
		} else {
			ui_volume_draw(notes);
		}
	}
}

void ui_refresh(void){
	if(cfg.hide_ui) return;

	ui_msg_draw();
	refresh();
}

void ui_quit(void){
	if(cfg.hide_ui) return;
	endwin();
}
