#include <SDL2/SDL.h>
#include "minigbs.h"

struct chan_len_ctr {
	int   load;
	bool  enabled;
	float counter;
	float inc;
};

struct chan_vol_env {
	int   step;
	bool  up;
	float counter;
	float inc;
};

struct chan_freq_sweep {
	int   freq;
	int   rate;
	bool  up;
	int   shift;
	float counter;
	float inc;
};

static struct chan {
	bool enabled;
	bool powered;
	bool on_left;
	bool on_right;
	bool user_mute;

	int volume;
	int volume_init;

	uint16_t freq;
	float    freq_counter;
	float    freq_inc;

	int val;
	int note;

	struct chan_len_ctr len;
	struct chan_vol_env env;
	struct chan_freq_sweep sweep;

	float capacitor;

	// square
	int duty;
	float fval;

	// noise
	uint16_t lfsr_reg;
	bool     lfsr_wide;
	int      lfsr_div;

	// wave
	int sample_cursor;
	float sample;
} chans[4];

#define FREQ 44100.0f

float audio_rate;

static size_t nsamples;
static float* samples;

static SDL_AudioDeviceID audio;
static const float duty[] = { 0.125, 0.25, 0.5, 0.75 };
static float logbase;
static float vol_l, vol_r;
static const char* notes[] = {
	"A-", "A#", "B-", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#"
};

float hipass(struct chan* c, float sample){
	float out = sample - c->capacitor;
	c->capacitor = sample - out * 0.996;
	return out;
}

void set_note_freq(struct chan* c, float freq){
	c->freq_inc = freq / FREQ;
	c->note = MAX(0, (int)roundf(logf(freq/440.0f) / logbase) + 48);
}

bool chan_muted(struct chan* c){
	return c->user_mute || !c->enabled || !(c->on_left || c->on_right) || !c->volume;
}

void chan_enable(int i, bool enable){
	chans[i].enabled = enable;

	uint8_t val = (mem[0xFF26] & 0x80)
		| (chans[3].enabled << 3)
		| (chans[2].enabled << 2)
		| (chans[1].enabled << 1)
		| (chans[0].enabled << 0);

	mem[0xFF26] = val;
}

void update_env(struct chan* c){
	c->env.counter += c->env.inc;

	while(c->env.counter > 1.0f){
		if(c->env.step){
			c->volume += c->env.up ? 1 : -1;
			if(c->volume == 0 || c->volume == 15){
				c->env.inc = 0;
			}
			c->volume = MAX(0, MIN(15, c->volume));
		}
		c->env.counter -= 1.0f;
	}
}

void update_len(struct chan* c){

	if(c->len.enabled){
		c->len.counter += c->len.inc;
		if(c->len.counter > 1.0f){
			chan_enable(c - chans, 0);
			c->len.counter = 0.0f;
		}
	}
}

int update_freq(struct chan* c){
	c->freq_counter += c->freq_inc;

	int result = 0;
	while(c->freq_counter > 1.0f){
		c->freq_counter -= 1.0f;
		result++;
	}

	return result;
}

void update_sweep(struct chan* c){
	c->sweep.counter += c->sweep.inc;

	while(c->sweep.counter > 1.0f){
		if(c->sweep.shift){
			uint16_t inc = (c->sweep.freq >>= c->sweep.shift);
			if(!c->sweep.up) inc *= -1;

			c->freq += inc;
			c->sweep.freq = c->freq;

			set_note_freq(c, 4194304 / (float)((2048 - c->freq) << 5));
		} else {
			c->enabled = 0;
		}
		c->sweep.counter -= 1.0f;
	}
}

float lerp(float a, float b, float t){
	return a + (b - a) * t;
}

void update_square(bool ch2){
	struct chan* c = chans + ch2;
	if(!c->powered) return;

	set_note_freq(c, 4194304.0f / (float)((2048 - c->freq) << 5));

	for(int i = 0; i < nsamples; i+=2){
		update_len(c);

		if(c->enabled){
			update_env(c);
			if(!ch2) update_sweep(c);

			float d;
			if(update_freq(c)){
				c->fval = (2*c->freq_counter - c->freq_inc) / c->freq_inc;
			} else if((d = c->freq_counter - duty[c->duty]) > 0.0f){
				c->fval = MAX(-1.0f, (c->freq_inc - 2*d) / c->freq_inc);
			} else {
				c->fval = 1.0f;
			}

			float sample = hipass(c, c->fval * (c->volume / 15.0f));

			if(!c->user_mute){
				samples[i+0] += sample * 0.25f * c->on_left * vol_l;
				samples[i+1] += sample * 0.25f * c->on_right * vol_r;
			}
		}
	}
}

void update_wave(void){
	struct chan* c = chans + 2;
	if(!c->powered) return;

	set_note_freq(c, 4194304 / (float)((2048 - c->freq) << 5));
	c->freq_inc *= 16.0f;

	for(int i = 0; i < nsamples; i+=2){
		update_len(c);

		if(c->enabled){
			float t = 1.0f;

			if(update_freq(c)){
				c->sample_cursor = c->val;
				c->val = (c->val + 1) & 31;
				t = c->freq_counter / c->freq_inc;
			}

			uint8_t s = mem[0xFF30 + c->sample_cursor / 2];
			if(c->sample_cursor & 1){
				s &= 0xF;
			} else {
				s >>= 4;
			}

			if(c->volume > 0){
				s >>= (c->volume - 1);
				float diff = (float[]){ 7.5f, 3.75f, 1.5f }[c->volume - 1];
				c->sample = lerp(c->sample, (float)s, t);

				float sample = hipass(c, (c->sample - diff) / 7.5f);
				if(!c->user_mute){
					samples[i+0] += sample * 0.25f * c->on_left * vol_l;
					samples[i+1] += sample * 0.25f * c->on_right * vol_r;
				}
			}
		}
	}
}

void update_noise(void){
	struct chan* c = chans + 3;
	if(!c->powered) return;

	float freq = 4194304.0f / (float)((int[]){ 8, 16, 32, 48, 64, 80, 96, 112 }[c->lfsr_div] << c->freq);
	set_note_freq(c, c->freq < 14 ? freq : 0.0f);

	for(int i = 0; i < nsamples; i+=2){
		update_len(c);

		if(c->enabled){
			update_env(c);

			float sample = c->val;
			int count = update_freq(c);
			for(int j = 0; j < count; ++j){
				c->lfsr_reg = (c->lfsr_reg << 1) | (c->val == 1);

				if(c->lfsr_wide){
					c->val = !(((c->lfsr_reg >> 14) & 1) ^ ((c->lfsr_reg >> 13) & 1)) ? 1 : -1;
				} else {
					c->val = !(((c->lfsr_reg >> 6 ) & 1) ^ ((c->lfsr_reg >> 5 ) & 1)) ? 1 : -1;
				}
				sample += c->val;
			}

			if(count){
				sample /= (float)count;
			}

			sample = hipass(c, sample * (c->volume / 15.0f));
			if(!c->user_mute){
				samples[i+0] += sample * 0.25f * c->on_left * vol_l;
				samples[i+1] += sample * 0.25f * c->on_right * vol_r;
			}
		}
	}
}

bool audio_mute(int chan, int val){
	chans[chan-1].user_mute = (val != -1) ? val : !chans[chan-1].user_mute;
	return chans[chan-1].user_mute;
}

void audio_output(bool redraw){
	memset(samples, 0, nsamples * sizeof(float));

	update_square(0);
	update_square(1);
	update_wave();
	update_noise();

	// draw notes
	if(redraw){
		move((cfg.win_h-6)/6+9, (cfg.win_w-47)/2+9);
		attron(A_BOLD);
		for(int i = 0; i < 3; ++i){
			if(!chan_muted(chans + i)){
				printw("[ ");
				attron(COLOR_PAIR(i+1));
				int octave = MAX(0, MIN(chans[i].note / 12, 9));
				printw("%s%d", notes[chans[i].note % 12], octave);
				attroff(COLOR_PAIR(i+1));
				printw(" ]     ");
			} else {
				printw("[     ]     ");
			}
		}
		attroff(A_BOLD);
	}

	for(int i = 0; i < nsamples; ++i){
		samples[i] *= cfg.volume;
	}

	uint64_t cur_size = SDL_GetQueuedAudioSize(audio) / (2 * sizeof(float));
	uint64_t max_size = FREQ / 8; // max 125ms buffer

	if(cur_size > max_size){
		usleep(((cur_size - max_size) / FREQ) * 1000000.0f);
	}
	
	SDL_QueueAudio(audio, samples, nsamples * sizeof(float));
}

void audio_pause(bool p){
	SDL_PauseAudioDevice(audio, p);
}

void audio_reset(void){
	memset(chans, 0, sizeof(chans));
	memset(samples, 0, nsamples * sizeof(float));
	SDL_ClearQueuedAudio(audio);
}

void audio_init(void){

	if(SDL_Init(SDL_INIT_AUDIO) != 0){
		fprintf(stderr, "Error calling SDL_Init: %s\n", SDL_GetError());
		exit(1);
	}

	SDL_AudioSpec want = {
		.freq     = FREQ,
		.channels = 2,
		.samples  = 512,
		.format   = AUDIO_F32SYS,
	};

	SDL_AudioSpec got;
	if((audio = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0)) == 0){
		printf("OpenAudio failed: %s.\n", SDL_GetError());
		exit(1);
	} else if(cfg.debug_mode){
		printf("Got audio: freq=%d, samples=%d, format=%d.\n", got.freq, got.samples, got.format);
	}

	logbase = log(1.059463094f);

	audio_update_rate();

	SDL_QueueAudio(audio, samples, nsamples * sizeof(float));
	SDL_PauseAudioDevice(audio, 0);
}

void audio_update_rate(void){
	audio_rate = 59.7f;

	uint8_t tma = mem[0xff06];
	uint8_t tac = mem[0xff07];

	if(tac & 0x04){
		int rates[] = { 4096, 262144, 65536, 16384 };
		audio_rate = rates[tac & 0x03] / (float)(256 - tma);
		if(tac & 0x80) audio_rate *= 2.0f;
	}

	if(cfg.debug_mode){
		printf("Audio rate changed: %.4f\n", audio_rate);
	}

	free(samples);
	nsamples = (int)(FREQ / audio_rate) * 2;
	samples  = calloc(nsamples, sizeof(float));
}

void chan_trigger(int i){
	struct chan* c = chans + i;

	if(cfg.debug_mode){
		static const char* cname[] = { "sq1", "sq2", "wave", "noise" };
		printf("(trigger %s)\n", cname[i]);
	}

	chan_enable(i, 1);
	c->volume = c->volume_init;

	// volume envelope
	{
		uint8_t val = mem[0xFF12 + (i*5)];

		c->env.step    = val & 0x07;
		c->env.up      = val & 0x08;
		c->env.inc     = c->env.step ? (64.0f / (float)c->env.step) / FREQ : 8.0f / FREQ;
		c->env.counter = 0.0f;
	}

	// freq sweep
	if(i == 0){
		uint8_t val = mem[0xFF10];

		c->sweep.freq    = c->freq;
		c->sweep.rate    = (val >> 4) & 0x07;
		c->sweep.up      = !(val & 0x08);
		c->sweep.shift   = (val & 0x07);
		c->sweep.inc     = c->sweep.rate ? (128.0f / (float)(c->sweep.rate + 1)) / FREQ : 0.0f;
		c->sweep.counter = 0.0f;
	}

	int len_max = 64;

	if(i == 2){ // wave
		len_max = 256;
	} else if(i == 3){ // noise
		c->lfsr_reg = 0xFFFF;
	}

	c->len.inc = (256.0f / (float)(len_max - c->len.load)) / FREQ;
	c->len.counter = 0.0f;

	if(i < 2){
		c->val = 1;
	} else {
		c->val = 0;
	}
}

void audio_write(uint16_t addr, uint8_t val){

	if(cfg.debug_mode){
		printf("Audio write: %4x <- %2x\n", addr, val);
	}

	int i = (addr - 0xFF10)/5;

	switch(addr){

		case 0xFF12:
		case 0xFF17:
		case 0xFF21: {
			chans[i].volume_init = val >> 4;
			chans[i].powered = val >> 3;

			// "zombie mode" stuff, needed for Prehistorik Man and probably others
			if(chans[i].powered && chans[i].enabled){

				if((chans[i].env.step == 0 && chans[i].env.inc != 0)){
					if(val & 0x08){
						if(cfg.debug_mode) puts("(zombie vol++)");
						chans[i].volume++;
					} else {
						if(cfg.debug_mode) puts("(zombie vol+=2)");
						chans[i].volume+=2;
					}
				} else {
					if(cfg.debug_mode) puts("(zombie swap)");
					chans[i].volume = 16 - chans[i].volume;
				}

				chans[i].volume &= 0x0F;
				chans[i].env.step = val & 0x07;
			}
		} break;

		case 0xFF1C:
			chans[i].volume = chans[i].volume_init = (val >> 5) & 0x03;
			break;

		case 0xFF11:
		case 0xFF16:
		case 0xFF20:
			chans[i].len.load = val & 0x3f;
			chans[i].duty = val >> 6;
			break;
			
		case 0xFF1B:
			chans[i].len.load = val;
			break;

		case 0xFF13:
		case 0xFF18:
		case 0xFF1D:
			chans[i].freq &= 0xFF00;
			chans[i].freq |= val;
			break;

		case 0xFF1A:
			chans[i].powered = val & 0x80;
			chan_enable(i, val & 0x80);
			break;

		case 0xFF14:
		case 0xFF19:
		case 0xFF1E:
			chans[i].freq &= 0x00FF;
			chans[i].freq |= ((val & 0x07) << 8);
		case 0xFF23:
			chans[i].len.enabled = val & 0x40;
			if(val & 0x80){
				chan_trigger(i);
			}
			break;

		case 0xFF22:
			chans[3].freq = val >> 4;
			chans[3].lfsr_wide = !(val & 0x08);
			chans[3].lfsr_div = val & 0x07;
			break;

		case 0xFF24:
			vol_l = ((val >> 4) & 0x07) / 7.0f;
			vol_r = (val & 0x07) / 7.0f;
			break;

		case 0xFF25: {
			for(int i = 0; i < 4; ++i){
				chans[i].on_left  = (val >> (4 + i)) & 1;
				chans[i].on_right = (val >> i) & 1;
			}
		} break;
	}

	mem[addr] = val;
}
