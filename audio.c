#include "minigbs.h"
#include <alsa/asoundlib.h>
#include <math.h>

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
	uint16_t freq;
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

#define FREQ 48000.0f

static size_t nsamples;
static float* samples;
static float* samples_tmp;
static float* sample_ptr;
static float* sample_end;

static const int duty_lookup[] = { 0x10, 0x30, 0x3C, 0xCF };
static float logbase;
static float charge_factor;
static float vol_l, vol_r;
static float audio_rate;
static bool  muted[4]; // not in chan struct to avoid memset(0) across tracks
static bool paused;

static snd_pcm_t* pcm;
static snd_pcm_uframes_t pcm_buffer_size;
static snd_pcm_uframes_t pcm_period_size;

float hipass(struct chan* c, float sample){
#if 1
	float out = sample - c->capacitor;
	c->capacitor = sample - out * charge_factor;
	return out;
#else
	return sample;
#endif
}

void set_note_freq(struct chan* c, float freq){
	c->freq_inc = freq / FREQ;
	c->note = MAX(0, (int)roundf(logf(freq/440.0f) / logbase) + 48);
}

bool chan_muted(struct chan* c){
	return muted[c-chans] || !c->enabled || !c->powered || !(c->on_left || c->on_right) || !c->volume;
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

			c->freq = c->sweep.freq + inc;
			if(c->freq > 2047){
				c->enabled = 0;
			} else {
				set_note_freq(c, 4194304.0f / (float)((2048 - c->freq) << 5));
				c->sweep.freq = c->freq;
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
				c->val = (c->duty & (1 << c->duty_counter)) ? 1 : -1;
				prev_pos = pos;
			}
			sample += ((pos - prev_pos) / c->freq_inc) * (float)c->val;
			sample = hipass(c, sample * (c->volume / 15.0f));

			if(!muted[c-chans]){
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

				if(!muted[c-chans]){
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

			if(!muted[c-chans]){
				samples[i+0] += sample * 0.25f * c->on_left * vol_l;
				samples[i+1] += sample * 0.25f * c->on_right * vol_r;
			}
		}
	}
}

bool audio_mute(int chan, int val){
	muted[chan-1] = (val != -1) ? val : !muted[chan-1];
	return muted[chan-1];
}

void audio_reset(void){
	memset(chans, 0, sizeof(chans));
	memset(samples, 0, nsamples * sizeof(float));
	sample_ptr = samples;
	sample_end = samples + nsamples;
	chans[0].val = chans[1].val = -1;
	audio_update_rate();
}

void audio_pause(bool p){
	paused = p;
}

void audio_update(struct pollfd* fds, int nfds){
	static float* buf = NULL;
	const size_t bufsz = (pcm_period_size*2) * sizeof(float);

	if(!buf){
		buf = malloc(bufsz);
	}

	float* p = buf;
	float* end = buf + (bufsz/sizeof(float));

	uint16_t ev;
	snd_pcm_poll_descriptors_revents(pcm, fds, nfds, &ev);

	if(!(ev & POLLOUT)){
		return;
	}

	if(paused){
		memset(buf, 0, bufsz);
		goto out;
	}

	while(end - p){
		if(sample_ptr == sample_end){
			cpu_frame();

			memset(samples    , 0, nsamples * sizeof(float));
			memset(samples_tmp, 0, nsamples * sizeof(float));

			update_square(0);
			ui_osc_draw(0, samples, nsamples);

			for(size_t i = 0; i < nsamples; ++i) samples_tmp[i] += samples[i];
			memset(samples, 0, nsamples * sizeof(float));

			update_square(1);
			ui_osc_draw(1, samples, nsamples);

			for(size_t i = 0; i < nsamples; ++i) samples_tmp[i] += samples[i];
			memset(samples, 0, nsamples * sizeof(float));

			update_wave();
			ui_osc_draw(2, samples, nsamples);

			for(size_t i = 0; i < nsamples; ++i) samples_tmp[i] += samples[i];
			memset(samples, 0, nsamples * sizeof(float));

			update_noise();
			ui_osc_draw(3, samples, nsamples);

			for(size_t i = 0; i < nsamples; ++i) samples[i] += samples_tmp[i];

			for(size_t i = 0; i < nsamples; ++i){
				samples[i] *= cfg.volume;
			}

			sample_ptr = samples;
		}

		int n = MIN(end - p, sample_end - sample_ptr);
		memcpy(p, sample_ptr, n * sizeof(float));
		sample_ptr += n;
		p += n;
	}

out:;
    int err = snd_pcm_writei(pcm, buf, pcm_period_size);
    if(err < 0){
	    snd_pcm_recover(pcm, err, 1);
    }
}

int audio_init(struct pollfd** fds, int nfds){
	snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT, SND_PCM_ACCESS_RW_INTERLEAVED, 2, FREQ, 1, 16667);
	snd_pcm_get_params(pcm, &pcm_buffer_size, &pcm_period_size);

	logbase = log(1.059463094f);
	charge_factor = pow(0.999958, 4194304.0 / FREQ);

	audio_update_rate();

	int count = snd_pcm_poll_descriptors_count(pcm);
	*fds = realloc(*fds, (nfds + count) * sizeof(struct pollfd));
	snd_pcm_poll_descriptors(pcm, (*fds) + nfds, count);

	return nfds + count;
}

void audio_quit(void){
	snd_pcm_close(pcm);
}

void audio_get_notes(uint16_t notes[static 4]){
	for(int i = 0; i < 4; ++i){
		if(chan_muted(chans + i)){
			notes[i] = 0xffff;
		} else {
			notes[i] = chans[i].note;
		}
	}
}

void audio_get_vol(uint8_t vol[static 8]){
	for(int i = 0; i < 4; ++i){
		vol[i*2+0] = chans[i].volume * chans[i].on_left;
		vol[i*2+1] = chans[i].volume * chans[i].on_right;
	}

	int ch3_hi = 0, ch3_lo = 0xf;
	for(uint8_t* p = mem + 0xFF30; p < mem + 0xFF40; ++p){
		uint8_t a = *p >> 4, b = *p & 0xF;
		ch3_lo = MIN(ch3_lo, MIN(a, b));
		ch3_hi = MAX(ch3_hi, MAX(a, b));
	}

	float ch3_v = (ch3_hi - ch3_lo) / 15.0f;

	if(vol[4]) vol[4] = 5*(4-vol[4]) * ch3_v;
	if(vol[5]) vol[5] = 5*(4-vol[5]) * ch3_v;
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

	audio_rate *= cfg.speed;

	debug_msg("Audio rate changed: %.4f", audio_rate);

	size_t new_nsamples = (int)(FREQ / audio_rate) * 2;
	float* new_samples = calloc(new_nsamples, sizeof(float));

	if(samples){
		memcpy(new_samples, samples, MIN(nsamples, new_nsamples));
	}

	free(samples);
	samples    = new_samples;
	nsamples   = new_nsamples;

	free(samples_tmp);
	samples_tmp = calloc(nsamples, sizeof(float));

	// TODO: these should really be adjusted more accurately to not lose samples on speed change
	sample_ptr = samples;
	sample_end = samples + nsamples;
}

void chan_trigger(int i){
	struct chan* c = chans + i;

	if(cfg.debug_mode){
		static const char* cname[] = { "sq1", "sq2", "wave", "noise" };
		debug_msg("Trigger %s", cname[i]);
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

	if(!cfg.subdued && mem[addr] != val){
		ui_regs_set(addr, audio_rate / 8);
	}

	int i = (addr - 0xFF10)/5;

	if(cfg.debug_mode){
		if(addr <= 0xFF26){
			int j = (addr - 0xFF10)%5;
			debug_msg("Audio write: %4x / NR%1d%1d <- %2x", addr, i+1, j, val);
		} else {
			debug_msg("Audio write: %4x <- %2x", addr, val);
		}
	}

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
						debug_msg("(zombie vol++)");
						chans[i].volume++;
					} else {
						debug_msg("(zombie vol+=2)");
						chans[i].volume+=2;
					}
				} else {
					debug_msg("(zombie swap)");
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
