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
	int duty_counter;

	// noise
	uint16_t lfsr_reg;
	bool     lfsr_wide;
	int      lfsr_div;

	// wave
	uint8_t sample;
} chans[4];

static bool mute[4];

#define FREQ 44100.0f

float audio_rate;
float audio_speed_modifier = 1.0f;

static size_t nsamples;
static float* samples;

static SDL_AudioDeviceID audio;
static const int duty_lookup[] = { 1, 2, 4, 6 };
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
	return mute[c-chans] || !c->enabled || !c->powered || !(c->on_left || c->on_right) || !c->volume;
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

bool update_freq(struct chan* c, float* pos){
	float inc = c->freq_inc - *pos;
	c->freq_counter += inc;

	if(c->freq_counter > 1.0f){
		*pos = c->freq_inc - (c->freq_counter - 1.0f);
		c->freq_counter = 0.0f;
		return true;
	} else {
		*pos = c->freq_inc;
		return false;
	}
}

void update_sweep(struct chan* c){
	c->sweep.counter += c->sweep.inc;

	while(c->sweep.counter > 1.0f){
		if(c->sweep.shift){
			uint16_t inc = (c->sweep.freq >> c->sweep.shift);
			if(!c->sweep.up) inc *= -1;

			c->freq += inc;
			if(c->freq > 2047){
				c->enabled = 0;
			} else {
				set_note_freq(c, 4194304.0f / (float)((2048 - c->freq) << 5));
				c->freq_inc *= 8.0f;
			}
		} else if(c->sweep.rate){
			c->enabled = 0;
		}
		c->sweep.counter -= 1.0f;
	}
}

void update_square(bool ch2){
	struct chan* c = chans + ch2;
	if(!c->powered) return;

	set_note_freq(c, 4194304.0f / (float)((2048 - c->freq) << 5));
	c->freq_inc *= 8.0f;

	for(int i = 0; i < nsamples; i+=2){
		update_len(c);

		if(c->enabled){
			update_env(c);
			if(!ch2) update_sweep(c);

			float pos = 0.0f;
			float prev_pos = 0.0f;
			float sample = 0.0f;

			while(update_freq(c, &pos)){
				c->duty_counter = (c->duty_counter + 1) & 7;
				sample += ((pos - prev_pos) / c->freq_inc) * (float)c->val;

				if(!c->duty_counter || c->duty_counter == c->duty){
					c->val = ~(c->val-1);
				}
				prev_pos = pos;
			}
			sample += ((pos - prev_pos) / c->freq_inc) * (float)c->val;
			sample = hipass(c, sample * (c->volume / 15.0f));

			if(!mute[c-chans]){
				samples[i+0] += sample * 0.25f * c->on_left * vol_l;
				samples[i+1] += sample * 0.25f * c->on_right * vol_r;
			}
		}
	}
}

static uint8_t wave_sample(int pos, int volume){
	uint8_t sample = mem[0xFF30 + pos / 2];
	if(pos & 1){
		sample &= 0xF;
	} else {
		sample >>= 4;
	}
	return volume ? (sample >> (volume-1)) : 0;
}

void update_wave(void){
	struct chan* c = chans + 2;
	if(!c->powered) return;

	float freq = 4194304.0f / (float)((2048 - c->freq) << 5);
	set_note_freq(c, freq);

	c->freq_inc *= 16.0f;

	for(int i = 0; i < nsamples; i+=2){
		update_len(c);

		if(c->enabled){
			float pos = 0.0f;
			float prev_pos = 0.0f;
			float sample = 0.0f;

			c->sample = wave_sample(c->val, c->volume);

			while(update_freq(c, &pos)){
				c->val = (c->val + 1) & 31;
				sample += ((pos - prev_pos) / c->freq_inc) * (float)c->sample;
				c->sample = wave_sample(c->val, c->volume);
				prev_pos = pos;
			}
			sample += ((pos - prev_pos) / c->freq_inc) * (float)c->sample;

			if(c->volume > 0){
				float diff = (float[]){ 7.5f, 3.75f, 1.5f }[c->volume - 1];
				sample = hipass(c, (sample - diff) / 7.5f);

				if(!mute[c-chans]){
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

	float freq = 4194304.0f / (float)((size_t[]){ 8, 16, 32, 48, 64, 80, 96, 112 }[c->lfsr_div] << (size_t)c->freq);
	set_note_freq(c, freq);

	if(c->freq >= 14){
		c->enabled = false;
	}

	for(int i = 0; i < nsamples; i+=2){
		update_len(c);

		if(c->enabled){
			update_env(c);

			float pos = 0.0f;
			float prev_pos = 0.0f;
			float sample = 0.0f;

			while(update_freq(c, &pos)){
				c->lfsr_reg = (c->lfsr_reg << 1) | (c->val == 1);

				if(c->lfsr_wide){
					c->val = !(((c->lfsr_reg >> 14) & 1) ^ ((c->lfsr_reg >> 13) & 1)) ? 1 : -1;
				} else {
					c->val = !(((c->lfsr_reg >> 6 ) & 1) ^ ((c->lfsr_reg >> 5 ) & 1)) ? 1 : -1;
				}
				sample += ((pos - prev_pos) / c->freq_inc) * c->val;
				prev_pos = pos;
			}
			sample += ((pos - prev_pos) / c->freq_inc) * c->val;
			sample = hipass(c, sample * (c->volume / 15.0f));

			if(!mute[c-chans]){
				samples[i+0] += sample * 0.25f * c->on_left * vol_l;
				samples[i+1] += sample * 0.25f * c->on_right * vol_r;
			}
		}
	}
}

bool audio_mute(int chan, int val){
	mute[chan-1] = (val != -1) ? val : !mute[chan-1];
	return mute[chan-1];
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
	chans[0].val = chans[1].val = 1;
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

	audio_rate *= audio_speed_modifier;

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
		c->sweep.inc     = c->sweep.rate ? (128.0f / (float)(c->sweep.rate)) / FREQ : 0;
		c->sweep.counter = nexttowardf(1.0f, 1.1f);
	}

	int len_max = 64;

	if(i == 2){ // wave
		len_max = 256;
		c->val = 0;
	} else if(i == 3){ // noise
		c->lfsr_reg = 0xFFFF;
		c->val = -1;
	}

	c->len.inc = (256.0f / (float)(len_max - c->len.load)) / FREQ;
	c->len.counter = 0.0f;
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
			chans[i].duty = duty_lookup[val >> 6];
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
