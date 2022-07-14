#include "minigbs.h"

struct audio_output* audio_output;

uint16_t audio_output_init(struct pollfd** fds, int* nfds, float freq) {
	return audio_output->init(audio_output, fds, nfds, freq);
}

void audio_output_quit(void) {
	audio_output->quit(audio_output);
}

bool audio_output_ready(struct pollfd* fds, int nfds) {
	return audio_output->ready(audio_output, fds, nfds);
}

void audio_output_write(const float* samples, uint16_t period_size) {
	audio_output->write(audio_output, samples, period_size);
}

/////// ALSA OUTPUT
#include <alsa/asoundlib.h>

struct audio_alsa {
	struct audio_output output;
	snd_pcm_t* pcm;
	snd_pcm_uframes_t pcm_buffer_size;
	snd_pcm_uframes_t pcm_period_size;
};

static uint16_t alsa_init(struct audio_output* out, struct pollfd** fds, int* nfds, float freq) {
	struct audio_alsa* alsa = (struct audio_alsa*)out;

	snd_pcm_open(&alsa->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_set_params(alsa->pcm, SND_PCM_FORMAT_FLOAT, SND_PCM_ACCESS_RW_INTERLEAVED, 2, freq, 1, 16667);
	snd_pcm_get_params(alsa->pcm, &alsa->pcm_buffer_size, &alsa->pcm_period_size);

	int count = snd_pcm_poll_descriptors_count(alsa->pcm);
	*fds = realloc(*fds, (*nfds + count) * sizeof(struct pollfd));
	snd_pcm_poll_descriptors(alsa->pcm, (*fds) + *nfds, count);

	*nfds += count;

	return alsa->pcm_period_size;
}

static void alsa_quit(struct audio_output* out) {
	struct audio_alsa* alsa = (struct audio_alsa*)out;

	snd_pcm_close(alsa->pcm);
}

static bool alsa_ready(struct audio_output* out, struct pollfd* fds, int nfds) {
	struct audio_alsa* alsa = (struct audio_alsa*)out;

	uint16_t ev;
	snd_pcm_poll_descriptors_revents(alsa->pcm, fds, nfds, &ev);

	return (ev & POLLOUT);
}

static void alsa_write(struct audio_output* out, const float* samples, uint16_t period_size) {
	struct audio_alsa* alsa = (struct audio_alsa*)out;

	int err = snd_pcm_writei(alsa->pcm, samples, period_size);
	if(err < 0){
		snd_pcm_recover(alsa->pcm, err, 1);
	}
}

static struct audio_alsa _output_alsa = {
	.output = {
		.interactive = true,
		.init  = alsa_init,
		.quit  = alsa_quit,
		.ready = alsa_ready,
		.write = alsa_write,
	}
};

struct audio_output* output_alsa = &_output_alsa.output;

/////// WAV OUTPUT

struct wav_writer;
struct wav_writer* wav_write_begin(const char* filename, uint32_t freq);
void wav_write_push (struct wav_writer* wav, const float* samples, uint16_t period_size);
void wav_write_end  (struct wav_writer* wav);

struct audio_wav {
	struct audio_output output;
	struct wav_writer* writer;
	int eventfd;
};

static uint16_t wav_init(struct audio_output* out, struct pollfd** fds, int* nfds, float freq) {
	struct audio_wav* wav = (struct audio_wav*)out;
	wav->writer = wav_write_begin(wav->output.filename, freq);
	if(!wav->writer) {
		fprintf(stderr, "Error opening .wav file\n");
		exit(1);
	}

	wav->eventfd = eventfd(1, 0);

	*fds = realloc(*fds, (*nfds + 1) * sizeof(struct pollfd));
	(*fds)[*nfds] = (struct pollfd){
		.fd = wav->eventfd,
		.events = POLLIN,
	};
	*nfds += 1;

	return freq / 60;
}

static void wav_quit(struct audio_output* out) {
	struct audio_wav* wav = (struct audio_wav*)out;
	wav_write_end(wav->writer);
}

static bool wav_ready(struct audio_output* out, struct pollfd* fds, int nfds) {
	return true;
}

static void wav_write(struct audio_output* out, const float* samples, uint16_t period_size) {
	struct audio_wav* wav = (struct audio_wav*)out;
	wav_write_push(wav->writer, samples, period_size);
}

static struct audio_wav _output_wav = {
	.output = {
		.interactive = false,
		.init  = wav_init,
		.quit  = wav_quit,
		.ready = wav_ready,
		.write = wav_write,
	}
};

struct audio_output* output_wav = &_output_wav.output;
