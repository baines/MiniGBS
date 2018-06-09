#include "minigbs.h"
#include <limits.h>
#include <float.h>
#include <assert.h>
#include <math.h>

int  x11_init       (void);
int  x11_action     (bool*);
void x11_draw_begin (void);
void x11_draw_lines (int16_t* points, size_t);
void x11_draw_end   (void);
void x11_toggle     (void);

#define GRID_W 60
#define GRID_H 60

bool ui_in_cmd_mode;

#define OSC_SAMPLES 1600
#define OSC_W 854

static uint8_t grid[GRID_W][GRID_H];
static uint8_t (*col)[GRID_H] = &grid[0];
static int boldness[16*3];
static int msg_timer;
static char msg[128];
static char input[32];
static char* input_ptr = input;

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
		move(cfg.win_h-1, 0);

		if(--msg_timer == 0){
			clrtoeol();
		} else {
			if(msg_timer > 15) attron(A_BOLD);
			printw("%s\n", msg);
			if(msg_timer > 15) attroff(A_BOLD);
		}
	} else if(ui_in_cmd_mode) {
		move(cfg.win_h-1, 0);
		clrtoeol();
		attron(A_BOLD);
		printw("Goto track: %s", input);
		attroff(A_BOLD);
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

int ui_init(void){
	if(cfg.hide_ui)
		return -1;

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

		if(can_change_color() && COLORS > 100){
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

	return x11_init();
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

static struct osc_chan {
	float samples[OSC_SAMPLES];
} osc_chans[4];

static ssize_t osc_find_transition(float* start, float* end){
	float* p = start+1;
	float* candidates[8];
	float** c = candidates;

	do {
		for(; p < end-1; ++p){
			if(*p > 0.01f){
				break;
			}
		}

		for(; p < end-1; ++p){
			if(*p < -0.01f){
				*c++ = p;
				break;
			}
		}
	} while(p < end-1 && c - candidates < countof(candidates));

	ssize_t off = -1;
	float max = FLT_MAX;
	for(float** f = candidates; f < c; ++f){
		float v = (*f)[0];
		if(v < max){
			max = v;
			off = *f - start;
		}
	}

	return off;
}

void ui_osc_draw(int chan, float* samples, size_t count){
	struct osc_chan* c = osc_chans + chan;

	count /= 2;

	if(count > OSC_SAMPLES){
		samples += 2 * (count - OSC_SAMPLES);
		count = OSC_SAMPLES;
	}

	size_t copy_count = OSC_SAMPLES - count;
	memmove(c->samples, c->samples + count, copy_count * sizeof(float));

	for(size_t i = 0; i < count; ++i){
		float s = ((samples[i*2] + samples[i*2+1]) / 2.0f);// * 1.2f * height;
		c->samples[copy_count + i] = s;
	}
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
	struct point {
		int16_t x, y;
	} points[OSC_W];

	for(size_t i = 0; i < OSC_W; ++i){
		points[i].x = i;
	}

	const int off_start = OSC_W / 2;
	const int off_end   = OSC_SAMPLES - off_start;
	const int height    = 120;

	x11_draw_begin();

	for(size_t i = 0; i < 4; ++i){
		struct osc_chan* c = osc_chans + i;

		const float y_mid = height / 2 + height * i;
		const ssize_t off = i == 3 ? OSC_SAMPLES - OSC_W : osc_find_transition(c->samples + off_start, c->samples + off_end);

		if(off != -1){
			for(size_t j = 0; j < OSC_W; ++j){
				points[j].y = y_mid + c->samples[j+off] * height;
			}
			x11_draw_lines((int16_t*)&points, OSC_W*2);
		}
	}

	x11_draw_end();
	if(cfg.hide_ui) return;

	ui_msg_draw();
	refresh();
}

void ui_quit(void){
	if(cfg.hide_ui) return;
	endwin();
}

void ui_reset(void){
	ui_in_cmd_mode = false;
	clear();
}

int ui_cmd(int key){
	int result = -1;

	if(cfg.hide_ui)
		return result;

	switch(key){
		case '\n': {
			if(ui_in_cmd_mode && *input){
				result = atoi(input);
			}
			input_ptr = input;
			*input = 0;
			ui_in_cmd_mode = !ui_in_cmd_mode;
			move(cfg.win_h-1, 0);
			clrtoeol();
		} break;

		case KEY_BACKSPACE: {
			if(input_ptr != input){
				*--input_ptr = 0;
			}
		} break;

		default: {
			if(input_ptr - input < sizeof(input)){
				*input_ptr++ = key;
				*input_ptr = 0;
			}
		} break;
	}

	return result;
}

int ui_action(int* out_val, bool* tui_events, bool* x11_events){
	int key = -1;

	if(*x11_events){
		key = x11_action(x11_events);
	}

	if(key == -1 && *tui_events){
		key = getch();
		if(key == -1){
			*tui_events = false;
			return -1;
		}
	}

	// TODO: more consistency here w.r.t ui messages being done here or at callsite etc.

	switch(key){
		case 27: // Escape
			if(getch() != -1) break;
		case 'q': {
			if(ui_in_cmd_mode){
				ui_cmd('\n');
			} else {
				return ACT_QUIT;
			}
		} break;

		case '1' ... '4': {
			if(!ui_in_cmd_mode){
				*out_val = key - '0';
				return ACT_CHAN_TOGGLE;
			}
		} // fall-through
		case '0':
		case '5' ... '9': {
			if(ui_in_cmd_mode){
				ui_cmd(key);
			}
		} break;

		case 'n':
		case KEY_RIGHT:
			*out_val = (cfg.song_no + 1) % cfg.song_count;
			ui_msg_set("Next\n");
			return ACT_TRACK_SET;

		case 'p':
		case KEY_LEFT:
			*out_val = (cfg.song_no + cfg.song_count - 1) % cfg.song_count;
			ui_msg_set("Previous\n");
			return ACT_TRACK_SET;

		case 'r':
		case KEY_UP:
			*out_val = cfg.song_no;
			ui_msg_set("Replay\n");
			return ACT_TRACK_SET;

		case 'c':
			cfg.ui_mode = (cfg.ui_mode != UI_MODE_CHART) ? UI_MODE_CHART : UI_MODE_REGISTERS;
			erase();
			break;

		case 'v':
			cfg.ui_mode = (cfg.ui_mode != UI_MODE_VOLUME) ? UI_MODE_VOLUME : UI_MODE_REGISTERS;
			erase();
			break;

		case ' ':
		case KEY_DOWN:
			return ACT_PAUSE;

		case '=':
		case '+': {
			float vol = MIN(1.0f, cfg.volume + 0.05f);
			*out_val = (int)roundf(100.0f * vol);
			return ACT_VOL;
		} break;

		case '-':
		case '_': {
			float vol = MAX(0.0f, cfg.volume - 0.05f);
			*out_val = (int)roundf(100.0f * vol);
			return ACT_VOL;
		} break;

		case '[':
			*out_val = -5;
			return ACT_SPEED;

		case ']':
			*out_val = 5;
			return ACT_SPEED;

		case KEY_BACKSPACE: {
			if(ui_in_cmd_mode){
				ui_cmd(key);
			} else {
				*out_val = 0;
				return ACT_SPEED;
			}
		} break;

		case '\n': {
			int track = ui_cmd(key);
			if(track != -1){
				*out_val = track;
				return ACT_TRACK_SET;
			}
		} break;

		case 'o': {
			x11_toggle();
		} break;

		case 12: // CTRL-L
			clear();
			break;
	}

	return -1;
}
