#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/eventfd.h>

struct regs;
struct GBSHeader;
struct Config;
struct pollfd;

void cpu_frame (void);

void debug_dump      (uint8_t* op, struct regs*);
void debug_separator (void);
void debug_msg       (const char* fmt, ...);

int  audio_init        (struct pollfd**, int);
void audio_quit        (void);
float audio_update     (struct pollfd*, int);
void audio_reset       (void);
void audio_write       (uint16_t addr, uint8_t val);
void audio_pause       (bool);
bool audio_mute        (int chan, int val);
void audio_update_rate (void);
void audio_get_notes   (uint16_t[static 4]);
void audio_get_vol     (uint8_t vol[static 8]);

struct audio_output {
	const bool interactive;

	uint16_t (*init)  (struct audio_output*, struct pollfd** fds, int* nfds, float freq);
	void     (*quit)  (struct audio_output*);
	bool     (*ready) (struct audio_output*, struct pollfd* fds, int nfds);
	void     (*write) (struct audio_output*, const float* samples, uint16_t period_size);

	const char* filename; // to be set by main code if interactive is false
};

uint16_t audio_output_init  (struct pollfd**, int* nfds, float freq);
void     audio_output_quit  (void);
bool     audio_output_ready (struct pollfd* fds, int nfds);
void     audio_output_write (const float* samples, uint16_t period_size);

extern struct audio_output* audio_output;
extern struct audio_output* output_alsa;
extern struct audio_output* output_wav;

int  ui_init      (void);
void ui_msg_set   (const char* fmt, ...);
void ui_regs_set  (uint16_t addr, int val);
void ui_chart_set (uint16_t[static 3]);
void ui_redraw    (struct GBSHeader*);
void ui_refresh   (void);
void ui_quit      (void);
void ui_reset     (void);
int  ui_cmd       (int key);
void ui_osc_draw  (int chan, float* samples, size_t n);
int  ui_action    (int* val, bool* tui, bool* x11);

extern bool ui_in_cmd_mode;

struct GBSHeader {
	char     id[3];
	uint8_t  version;
	uint8_t  song_count;
	uint8_t  start_song;
	uint16_t load_addr;
	uint16_t init_addr;
	uint16_t play_addr;
	uint16_t sp;
	uint8_t  tma;
	uint8_t  tac;
	char     title[32];
	char     author[32];
	char     copyright[32];
} __attribute__((packed));

struct regs {
	union {
		uint16_t af;
		struct {
			union {
				struct { uint8_t _pad:4, c:1, h:1, n:1, z:1; };
				uint8_t all;
			} flags;
			uint8_t a;
		};
	};
	union { uint16_t bc; struct { uint8_t c, b; }; };
	union { uint16_t de; struct { uint8_t e, d; }; };
	union { uint16_t hl; struct { uint8_t l, h; }; };
	uint16_t sp, pc;
};

enum UIMode {
	UI_MODE_REGISTERS,
	UI_MODE_VOLUME,
	UI_MODE_CHART,

	UI_MODE_COUNT,
};

enum UIAction {
	ACT_QUIT,
	ACT_CHAN_TOGGLE,
	ACT_TRACK_SET,
	ACT_PAUSE,
	ACT_VOL,
	ACT_SPEED,
};

struct Config {
	int debug_mode;
	bool monochrome;
	bool hide_ui;
	bool subdued;

	bool write_wav;

	const char* output_filename;
	float output_duration_ms;

	int song_no;
	int song_count;

	float volume; // 0.0f - 1.0f
	float speed;  // 0.0f - 1.0f

	enum UIMode ui_mode;

	int win_w, win_h;
};

extern struct Config cfg;
extern uint8_t* mem;

#define MAX(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a >  _b ? _a : _b; })
#define MIN(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a <= _b ? _a : _b; })
#define countof(x) (sizeof(x)/sizeof(*x))
