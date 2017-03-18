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

	// square
	int duty;

	// noise
	uint16_t lfsr_reg;
	bool     lfsr_wide;
	int      lfsr_div;
} chans[4];

#define FREQ 44100
#define FREQF ((float)FREQ)
#define HZ 59.7f
#define SAMPLES ((FREQ / (int)HZ))
#define DBLSAMPLES (SAMPLES*2)

static float samples[DBLSAMPLES];
static SDL_AudioDeviceID audio;
static const float duty[] = { 0.125, 0.25, 0.5, 0.25 };
static float logbase;
static float vol_l, vol_r;
static const char* notes[] = {
	"A-", "A#", "B-", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#"
};

void set_note_freq(struct chan* c, float freq){
	c->freq_inc = freq / FREQF;
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
		c->volume += c->env.up ? 1 : -1;
		c->volume = MAX(0, MIN(15, c->volume));
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

void update_square(bool ch2){
	struct chan* c = chans + ch2;

	set_note_freq(c, 4194304 / (float)((2048 - c->freq) << 5));

	for(int i = 0; i < DBLSAMPLES; i+=2){
		update_len(c);

		if(c->enabled){
			update_env(c);
			if(!ch2) update_sweep(c);

			if(update_freq(c)){
				c->val = 1;
			} else if(c->freq_counter > duty[c->duty]){
				c->val = -1;
			}

			if(!chan_muted(c)){
				samples[i+0] += c->val * (c->volume / 15.0f) * 0.25 * c->on_left * vol_l;
				samples[i+1] += c->val * (c->volume / 15.0f) * 0.25 * c->on_right * vol_r;
			}
		}
	}
}

void update_wave(void){
	struct chan* c = chans + 2;

	set_note_freq(c, 4194304 / (float)((2048 - c->freq) << 5));
	c->freq_inc *= 16.0f;

	for(int i = 0; i < DBLSAMPLES; i+=2){
		update_len(c);

		if(c->enabled){
			c->val = (c->val + update_freq(c)) & 31;

			uint8_t s = mem[0xFF30 + c->val / 2];
			if(c->val & 1){
				s &= 0xF;
			} else {
				s >>= 4;
			}

			if(c->volume > 0){
				s >>= (c->volume - 1);
				float diff = (float[]){ 7.5f, 3.75f, 1.875 }[c->volume - 1];
				float ss = (float)s;

				if(!chan_muted(c)){
					samples[i+0] += ((ss - diff) / 30.0f) * c->on_left * vol_l;
					samples[i+1] += ((ss - diff) / 30.0f) * c->on_right * vol_r;
				}
			}
		}
	}
}

void update_noise(void){
	struct chan* c = chans + 3;

	float freq = 4194304.0f / (float)((int[]){ 8, 16, 32, 48, 64, 80, 96, 112 }[c->lfsr_div] << c->freq);
	set_note_freq(c, c->freq < 14 ? freq : 0.0f);

	for(int i = 0; i < DBLSAMPLES; i+=2){
		update_len(c);

		if(c->enabled){
			update_env(c);

			if(update_freq(c)){
				c->lfsr_reg = (c->lfsr_reg << 1) | (c->val == 1);

				if(c->lfsr_wide){
					c->val = !(((c->lfsr_reg >> 14) & 1) ^ ((c->lfsr_reg >> 13) & 1)) ? 1 : -1;
				} else {
					c->val = !(((c->lfsr_reg >> 6 ) & 1) ^ ((c->lfsr_reg >> 5 ) & 1)) ? 1 : -1;
				}
			}

			if(!chan_muted(c)){
				samples[i+0] += c->val * (c->volume / 15.0f) * 0.25f * c->on_left * vol_l;
				samples[i+1] += c->val * (c->volume / 15.0f) * 0.25f * c->on_right * vol_r;
			}
		}
	}
}

bool audio_mute(int chan, int val){
	chans[chan-1].user_mute = (val != -1) ? val : !chans[chan-1].user_mute;
	return chans[chan-1].user_mute;
}

void audio_output(){
	memset(samples, 0, sizeof(samples));

	update_square(0);
	update_square(1);
	update_wave();
	update_noise();

	// draw notes
	if(!cfg.hide_ui){
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

	for(int i = 0; i < SAMPLES*2; ++i){
		samples[i] *= cfg.volume;
	}

	while(SDL_GetQueuedAudioSize(audio) > (SAMPLES * sizeof(float) * 15)){
		usleep(250);
	}
	
	SDL_QueueAudio(audio, samples, sizeof(samples));
}

void audio_pause(bool p){
	SDL_PauseAudioDevice(audio, p);
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

	SDL_QueueAudio(audio, samples, sizeof(samples));
	SDL_PauseAudioDevice(audio, 0);
}

void chan_trigger(int i){
	struct chan* c = chans + i;
	
	chan_enable(i, 1);
	c->volume = c->volume_init;

	// volume envelope
	{
		uint8_t val = mem[0xFF12 + (i*5)];

		c->env.step    = val & 0x07;
		c->env.up      = val & 0x08;
		c->env.inc     = c->env.step ? (64.0f / (float)c->env.step) / FREQF : 0.0f;
		c->env.counter = 0.0f;
	}

	// freq sweep
	if(i == 0){
		uint8_t val = mem[0xFF10];

		c->sweep.freq    = c->freq;
		c->sweep.rate    = (val >> 4) & 0x07;
		c->sweep.up      = !(val & 0x08);
		c->sweep.shift   = (val & 0x07);
		c->sweep.inc     = c->sweep.rate ? (128.0f / (float)(c->sweep.rate + 1)) / FREQF : 0.0f;
		c->sweep.counter = 0.0f;
	}

	int len_max = 64;

	if(i == 2){ // wave
		len_max = 256;
	} else if(i == 3){ // noise
		c->lfsr_reg = 0xFFFF;
	}

	c->len.inc = (256.0f / (float)(len_max - c->len.load)) / FREQF;
	c->len.counter = 0.0f;
	
	c->freq_counter = 0.0f;

	c->val = 0;
}

void audio_write(uint16_t addr, uint8_t val){

	if(cfg.debug_mode){
		printf("Audio write: %4x <- %2x\n", addr, val);
	}

	int i = (addr - 0xFF10)/5;

	switch(addr){

		case 0xFF12:
		case 0xFF17:
		case 0xFF21:
			chans[i].volume = chans[i].volume_init = val >> 4;
			break;

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
