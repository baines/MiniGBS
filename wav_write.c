#include "minigbs.h"

struct wav_header {
	uint8_t  riff_magic[4];
	uint32_t chunk_size;
	uint8_t  wave_magic[4];

	uint8_t  fmt_magic[4];
	uint32_t fmt_size;
	uint16_t format;
	uint16_t nchannels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;

	uint8_t  data_magic[4];
	uint32_t data_size;
};

struct wav_writer {
	struct wav_header header;
	FILE* file;
	uint32_t data_size;
};

struct wav_writer* wav_write_begin(const char* filename, uint32_t freq) {
	struct wav_writer* wav = calloc(1, sizeof(struct wav_writer));

	*wav = (struct wav_writer){
		.header = {
			.riff_magic = "RIFF",
			.wave_magic = "WAVE",
			.fmt_magic  = "fmt ",
			.fmt_size   = 16,
			.format     = 3,
			.nchannels  = 2,
			.sample_rate = freq,
			.byte_rate = freq * 2 * sizeof(float),
			.block_align = 2 * sizeof(float),
			.bits_per_sample = sizeof(float) * 8,
			.data_magic = "data",
		},
	};

	wav->file = fopen(filename, "w");
	if(!wav->file) {
		fprintf(stderr, "fopen(%s): %m\n", filename);
		return NULL;
	}

	fwrite(&wav->header, sizeof(wav->header), 1, wav->file);

	return wav;
}

void wav_write_push(struct wav_writer* wav, const float* samples, uint16_t period_size) {
	uint32_t size = period_size * sizeof(float) * 2;
	wav->data_size += size;
	fwrite(samples, size, 1, wav->file);
}

void wav_write_end(struct wav_writer* wav) {

	uint32_t size = 36 + wav->data_size;
	fseek(wav->file, 4, SEEK_SET);
	fwrite(&size, 4, 1, wav->file);

	fseek(wav->file, 40, SEEK_SET);
	fwrite(&wav->data_size, 4, 1, wav->file);

	fflush(wav->file);
	fclose(wav->file);

	free(wav);
}
