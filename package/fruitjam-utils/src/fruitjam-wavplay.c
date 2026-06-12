// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam WAV tone player.
 *
 * This is a no-MMU friendly first WAV path for the TLV320DAC3100. It reads a
 * simple PCM WAV file, estimates monophonic tone windows, and plays those
 * tones through the tiny PIO I2S tone path. It is intended for
 * simple sine-wave songs while a full streamed PCM path is still under bring-up.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif

#define TLV_ADDR 0x18
#define PERIPH_RESET_GPIO 22u
#define AUDIO_SAMPLE_RATE 8000u
#define WINDOW_MS 200u
#define MAX_SEGMENTS 192u
#define SILENCE_LEVEL 180

enum tone_backend {
	TONE_BACKEND_BEEP,
	TONE_BACKEND_I2S,
};

struct wav_fmt {
	uint16_t audio_format;
	uint16_t channels;
	uint32_t sample_rate;
	uint16_t bits_per_sample;
	uint32_t data_offset;
	uint32_t data_size;
};

struct segment {
	unsigned int hz;
	unsigned int ms;
};

static struct segment segments[MAX_SEGMENTS];
static uint8_t wav_buf[1024];

static void usage(void)
{
	fprintf(stderr,
		"usage: fruitjam-wavplay [--i2s|--beep] [--loud] [--analyze] FILE.wav\n");
}

static void sleep_ms(unsigned int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

static const int16_t sine_q15[] = {
	    0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
	 6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
	12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
	18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
	23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
	27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
	30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
	32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
	32767,
};

static int16_t sine_from_phase(uint32_t phase)
{
	uint32_t p = phase >> 16;
	uint32_t quadrant = p >> 14;
	uint32_t idx = (p & 0x3fff) >> 8;
	int32_t v;

	if (quadrant & 1)
		idx = 64 - idx;
	v = sine_q15[idx];
	if (quadrant >= 2)
		v = -v;
	return (int16_t)v;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_full(int fd, void *buf, size_t len)
{
	uint8_t *p = buf;
	size_t done = 0;

	while (done < len) {
		ssize_t ret = read(fd, p + done, len - done);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!ret)
			return -1;
		done += (size_t)ret;
	}
	return 0;
}

static int skip_bytes(int fd, uint32_t len)
{
	char buf[64];

	while (len) {
		size_t chunk = len > sizeof(buf) ? sizeof(buf) : len;

		if (read_full(fd, buf, chunk) < 0)
			return -1;
		len -= (uint32_t)chunk;
	}
	return 0;
}

static int parse_wav(int fd, struct wav_fmt *fmt)
{
	uint8_t hdr[12];
	int got_fmt = 0;
	int got_data = 0;

	memset(fmt, 0, sizeof(*fmt));
	if (read_full(fd, hdr, sizeof(hdr)) < 0 ||
	    memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
		fprintf(stderr, "fruitjam-wavplay: not a RIFF/WAVE file\n");
		return -1;
	}

	while (!got_data) {
		uint8_t chdr[8];
		uint32_t size;
		off_t data_start;

		if (read_full(fd, chdr, sizeof(chdr)) < 0)
			return -1;
		size = le32(chdr + 4);
		data_start = lseek(fd, 0, SEEK_CUR);
		if (data_start < 0)
			return -1;

		if (!memcmp(chdr, "fmt ", 4)) {
			uint8_t f[16];

			if (size < sizeof(f) || read_full(fd, f, sizeof(f)) < 0)
				return -1;
			fmt->audio_format = le16(f);
			fmt->channels = le16(f + 2);
			fmt->sample_rate = le32(f + 4);
			fmt->bits_per_sample = le16(f + 14);
			if (size > sizeof(f) && skip_bytes(fd, size - sizeof(f)) < 0)
				return -1;
			got_fmt = 1;
		} else if (!memcmp(chdr, "data", 4)) {
			fmt->data_offset = (uint32_t)data_start;
			fmt->data_size = size;
			got_data = 1;
		} else if (skip_bytes(fd, size) < 0) {
			return -1;
		}
		if (size & 1) {
			uint8_t pad;

			if (read_full(fd, &pad, 1) < 0)
				return -1;
		}
	}

	if (!got_fmt || fmt->audio_format != 1 ||
	    (fmt->channels != 1 && fmt->channels != 2) ||
	    (fmt->bits_per_sample != 8 && fmt->bits_per_sample != 16) ||
	    fmt->sample_rate < 4000 || fmt->sample_rate > 48000) {
		fprintf(stderr,
			"fruitjam-wavplay: unsupported WAV format fmt=%u ch=%u rate=%lu bits=%u\n",
			fmt->audio_format, fmt->channels,
			(unsigned long)fmt->sample_rate, fmt->bits_per_sample);
		return -1;
	}
	return 0;
}

static unsigned int close_hz(unsigned int a, unsigned int b)
{
	unsigned int diff;

	if (a == b)
		return 1;
	if (!a || !b)
		return 0;
	diff = a > b ? a - b : b - a;
	return diff * 100 <= a * 4;
}

static void add_segment(struct segment *segs, unsigned int *count,
			unsigned int hz, unsigned int ms)
{
	struct segment *last;

	if (!ms)
		return;
	if (*count) {
		last = &segs[*count - 1];
		if ((!hz && !last->hz) || close_hz(last->hz, hz)) {
			if (last->hz && hz)
				last->hz = (last->hz + hz) / 2;
			last->ms += ms;
			return;
		}
	}
	if (*count >= MAX_SEGMENTS) {
		segs[MAX_SEGMENTS - 1].ms += ms;
		return;
	}
	segs[*count].hz = hz;
	segs[*count].ms = ms;
	(*count)++;
}

static int analyze_wav(int fd, const struct wav_fmt *fmt,
		       struct segment *segs, unsigned int *seg_count)
{
	uint32_t bytes_per_frame = fmt->channels * (fmt->bits_per_sample / 8u);
	uint32_t total_frames = fmt->data_size / bytes_per_frame;
	uint32_t window_frames = (fmt->sample_rate * WINDOW_MS) / 1000u;
	uint32_t frame = 0;

	*seg_count = 0;
	if (!window_frames)
		window_frames = 1;
	if (lseek(fd, fmt->data_offset, SEEK_SET) < 0)
		return -1;

	while (frame < total_frames) {
		uint32_t frames = total_frames - frame;
		uint64_t sum_abs = 0;
		unsigned int crossings = 0;
		int prev_sign = 0;
		unsigned int active = 0;
		unsigned int hz = 0;
		unsigned int ms;
		uint32_t remaining;

		if (frames > window_frames)
			frames = window_frames;
		remaining = frames;
		while (remaining) {
			uint32_t chunk_frames = sizeof(wav_buf) / bytes_per_frame;
			uint32_t j;

			if (!chunk_frames)
				return -1;
			if (chunk_frames > remaining)
				chunk_frames = remaining;
			if (read_full(fd, wav_buf,
				      chunk_frames * bytes_per_frame) < 0)
				return -1;

			for (j = 0; j < chunk_frames; j++) {
				const uint8_t *p = wav_buf + j * bytes_per_frame;
				int left;
				int right;
				int sample;
				int mag;
				int sign = 0;

				if (fmt->bits_per_sample == 8) {
					left = ((int)p[0] - 128) << 8;
					right = fmt->channels == 2 ?
						((int)p[1] - 128) << 8 : left;
				} else {
					left = (int16_t)le16(p);
					right = fmt->channels == 2 ?
						(int16_t)le16(p + 2) : left;
				}
				sample = (left + right) / 2;
				mag = sample < 0 ? -sample : sample;
				sum_abs += (uint32_t)mag;
				if (mag > SILENCE_LEVEL)
					active++;
				if (sample > SILENCE_LEVEL)
					sign = 1;
				else if (sample < -SILENCE_LEVEL)
					sign = -1;
				if (sign && prev_sign && sign != prev_sign)
					crossings++;
				if (sign)
					prev_sign = sign;
			}
			remaining -= chunk_frames;
		}
		ms = (unsigned int)(((uint64_t)frames * 1000u +
				    fmt->sample_rate / 2u) / fmt->sample_rate);
		if (active > frames / 5 &&
		    sum_abs / frames > SILENCE_LEVEL && crossings >= 2) {
			hz = (unsigned int)(((uint64_t)crossings * fmt->sample_rate +
					     frames) / (2u * frames));
			if (hz < 30)
				hz = 0;
			if (hz > AUDIO_SAMPLE_RATE / 2u - 1u)
				hz = AUDIO_SAMPLE_RATE / 2u - 1u;
		}
		add_segment(segs, seg_count, hz, ms);
		frame += frames;
	}
	return 0;
}

static int audio_clock_cmd(const char *cmd)
{
	int fd = open("/dev/fruitjam-audio", O_WRONLY);
	ssize_t ret;

	if (fd < 0) {
		fprintf(stderr, "fruitjam-wavplay: open /dev/fruitjam-audio: %s\n",
			strerror(errno));
		return -1;
	}
	ret = write(fd, cmd, strlen(cmd));
	close(fd);
	if (ret != (ssize_t)strlen(cmd)) {
		fprintf(stderr, "fruitjam-wavplay: write /dev/fruitjam-audio: %s\n",
			ret < 0 ? strerror(errno) : "short write");
		return -1;
	}
	return 0;
}

static int write_text_path(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_TRUNC);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, strlen(text));
	close(fd);
	return ret == (ssize_t)strlen(text) ? 0 : -1;
}

static int gpio_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "/sys/class/gpio/gpio%u/%s", gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int export_gpio(unsigned int gpio)
{
	char path[64];
	char text[12];
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	ssize_t ret;
	int i;

	if (fd < 0)
		return -1;
	snprintf(text, sizeof(text), "%u", gpio);
	ret = write(fd, text, strlen(text));
	close(fd);
	if (ret < 0 && errno != EBUSY)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	for (i = 0; i < 50; i++) {
		if (access(path, F_OK) == 0)
			return 0;
		sleep_ms(2);
	}
	return -1;
}

static void release_peripheral_reset(void)
{
	const char *mode = getenv("FRUITJAM_AUDIO_RESET");
	char path[64];

	if (!mode || !strcmp(mode, "0") || !strcmp(mode, "off"))
		return;
	if (export_gpio(PERIPH_RESET_GPIO) < 0)
		return;
	if (gpio_path(PERIPH_RESET_GPIO, "direction", path, sizeof(path)) == 0)
		(void)write_text_path(path, "out");
	if (gpio_path(PERIPH_RESET_GPIO, "value", path, sizeof(path)) == 0) {
		if (mode && !strcmp(mode, "pulse")) {
			(void)write_text_path(path, "0");
			sleep_ms(100);
		}
		(void)write_text_path(path, "1");
	}
	sleep_ms(50);
}

static int audio_tone(unsigned int hz, unsigned int ms)
{
	char cmd[32];

	if (!hz) {
		sleep_ms(ms);
		return 0;
	}
	if (ms > 15000u)
		ms = 15000u;
	snprintf(cmd, sizeof(cmd), "tone %u %u", hz, ms);
	return audio_clock_cmd(cmd);
}

static int i2c_write_reg(int fd, uint8_t page, uint8_t reg, uint8_t val)
{
	uint8_t buf[2];

	buf[0] = 0x00;
	buf[1] = page;
	if (write(fd, buf, 2) != 2)
		return -1;
	buf[0] = reg;
	buf[1] = val;
	return write(fd, buf, 2) == 2 ? 0 : -1;
}

static int codec_open(void)
{
	int fd = open("/dev/i2c-0", O_RDWR);

	if (fd < 0) {
		perror("fruitjam-wavplay: open /dev/i2c-0");
		return -1;
	}
	if (ioctl(fd, I2C_SLAVE, TLV_ADDR) < 0) {
		perror("fruitjam-wavplay: I2C_SLAVE");
		close(fd);
		return -1;
	}
	return fd;
}

static int codec_beep_tone(int fd, unsigned int hz, unsigned int ms,
			   unsigned int volume)
{
	uint32_t samples;
	uint32_t phase;
	int16_t sinv;
	int16_t cosv;

	if (!hz) {
		sleep_ms(ms);
		return 0;
	}
	if (ms > 15000u)
		ms = 15000u;
	if (hz >= AUDIO_SAMPLE_RATE / 2)
		hz = AUDIO_SAMPLE_RATE / 2 - 1;
	samples = (uint32_t)(((uint64_t)ms * AUDIO_SAMPLE_RATE) / 1000u);
	if (!samples)
		samples = 1;
	if (samples > 0x00ffffffu)
		samples = 0x00ffffffu;

	phase = (uint32_t)(((uint64_t)hz << 32) / AUDIO_SAMPLE_RATE);
	sinv = sine_from_phase(phase);
	cosv = sine_from_phase(phase + 0x40000000u);
	volume &= 0x3f;

	if (i2c_write_reg(fd, 0, 0x47, volume) < 0 ||
	    i2c_write_reg(fd, 0, 0x48, volume) < 0 ||
	    i2c_write_reg(fd, 0, 0x49, (samples >> 16) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4a, (samples >> 8) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4b, samples & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4c, ((uint16_t)sinv >> 8) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4d, (uint16_t)sinv & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4e, ((uint16_t)cosv >> 8) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4f, (uint16_t)cosv & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x47, 0x80 | volume) < 0) {
		fprintf(stderr, "fruitjam-wavplay: i2c write beep %u Hz\n", hz);
		return -1;
	}
	sleep_ms(ms + 8);
	return 0;
}

static int play_tone(int fd, enum tone_backend backend, unsigned int hz,
		     unsigned int ms, unsigned int beep_volume)
{
	if (backend == TONE_BACKEND_I2S)
		return audio_tone(hz, ms);
	return codec_beep_tone(fd, hz, ms, beep_volume);
}

static int codec_init(int fd, int loud, enum tone_backend backend)
{
	uint8_t dac_vol = loud ? 0xe8 : 0xd8;
	uint8_t hp_vol = loud ? 0x94 : 0xa8;
	uint8_t spk_vol = loud ? 0x8c : 0x94;

#define W(page, reg, val) do { \
	if (i2c_write_reg(fd, (page), (reg), (val)) < 0) { \
		fprintf(stderr, "fruitjam-wavplay: i2c write p%u r0x%02x\n", \
			(unsigned int)(page), (unsigned int)(reg)); \
		return -1; \
	} \
} while (0)

	W(0, 0x01, 0x01);
	sleep_ms(20);
	W(0, 0x3f, 0x14);       /* DACs off, normal paths */
	W(0, 0x05, 0x11);       /* PLL P=1, R=1, off */
	W(0, 0x06, 0x06);
	W(0, 0x07, 0x25);
	W(0, 0x08, 0xa0);
	W(0, 0x04, 0x00);       /* PLL input is MCLK */
	W(0, 0x05, 0x91);       /* PLL on */
	sleep_ms(10);
	W(0, 0x04, 0x03);       /* CODEC_CLKIN is PLL */
	W(0, 0x1b, 0x00);       /* I2S, 16-bit stereo, BCLK/WCLK input */
	W(0, 0x0b, 0x91);
	W(0, 0x0c, 0x81);
	W(0, 0x0d, 0x03);
	W(0, 0x0e, 0x00);
	W(0, 0x3c, backend == TONE_BACKEND_BEEP ? 0x19 : 0x01);
	sleep_ms(30);
	W(0, 0x3f, 0xd4);
	W(0, 0x40, 0x00);
	W(0, 0x41, dac_vol);
	W(0, 0x42, dac_vol);
	W(1, 0x23, 0x44);
	W(1, 0x1f, 0xd4);
	W(1, 0x24, hp_vol);
	W(1, 0x25, hp_vol);
	W(1, 0x26, spk_vol);
	W(1, 0x28, 0x34);
	W(1, 0x29, 0x34);
	W(1, 0x2a, 0x0c);
	W(1, 0x20, 0x80);
	sleep_ms(350);
#undef W
	return 0;
}

static void print_analysis(const char *wav_path, const struct wav_fmt *fmt,
			   const struct segment *segs, unsigned int seg_count)
{
	unsigned int i;
	unsigned long total_ms = 0;

	for (i = 0; i < seg_count; i++)
		total_ms += segs[i].ms;

	printf("fruitjam-wavplay: %s ch=%u rate=%lu bits=%u segments=%u duration_ms=%lu\n",
	       wav_path, fmt->channels, (unsigned long)fmt->sample_rate,
	       fmt->bits_per_sample, seg_count, total_ms);
	for (i = 0; i < seg_count; i++)
		printf("segment %u hz=%u ms=%u\n", i, segs[i].hz, segs[i].ms);
}

int main(int argc, char **argv)
{
	struct wav_fmt fmt;
	const char *wav_path = NULL;
	enum tone_backend backend = TONE_BACKEND_I2S;
	unsigned int seg_count = 0;
	unsigned int beep_volume;
	unsigned int i;
	int loud = 0;
	int analyze_only = 0;
	int argi;
	int wav_fd;
	int i2c_fd;
	int ret = 0;

	for (argi = 1; argi < argc; argi++) {
		if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
			usage();
			return 0;
		}
		if (!strcmp(argv[argi], "--beep")) {
			backend = TONE_BACKEND_BEEP;
			continue;
		}
		if (!strcmp(argv[argi], "--i2s")) {
			backend = TONE_BACKEND_I2S;
			continue;
		}
		if (!strcmp(argv[argi], "--loud")) {
			loud = 1;
			continue;
		}
		if (!strcmp(argv[argi], "--analyze")) {
			analyze_only = 1;
			continue;
		}
		if (wav_path) {
			usage();
			return 1;
		}
		wav_path = argv[argi];
	}
	if (!wav_path) {
		usage();
		return 1;
	}
	beep_volume = loud ? 2u : 12u;

	wav_fd = open(wav_path, O_RDONLY);
	if (wav_fd < 0) {
		fprintf(stderr, "fruitjam-wavplay: open %s: %s\n",
			wav_path, strerror(errno));
		return 1;
	}
	if (parse_wav(wav_fd, &fmt) < 0 ||
	    analyze_wav(wav_fd, &fmt, segments, &seg_count) < 0) {
		close(wav_fd);
		return 1;
	}
	close(wav_fd);
	if (!seg_count) {
		fprintf(stderr, "fruitjam-wavplay: no playable tone segments\n");
		return 1;
	}
	if (analyze_only) {
		print_analysis(wav_path, &fmt, segments, seg_count);
		return 0;
	}

	release_peripheral_reset();
	if (audio_clock_cmd("start") < 0)
		return 1;
	i2c_fd = codec_open();
	if (i2c_fd < 0) {
		(void)audio_clock_cmd("stop");
		return 1;
	}
	if (codec_init(i2c_fd, loud, backend) < 0) {
		close(i2c_fd);
		(void)audio_clock_cmd("stop");
		return 1;
	}

	print_analysis(wav_path, &fmt, segments, seg_count);
	printf("fruitjam-wavplay: backend=%s\n",
	       backend == TONE_BACKEND_I2S ? "i2s" : "beep");
	for (i = 0; i < seg_count; i++) {
		if (play_tone(i2c_fd, backend, segments[i].hz, segments[i].ms,
			      beep_volume) < 0) {
			fprintf(stderr, "fruitjam-wavplay: playback failed\n");
			ret = 1;
			break;
		}
	}
	close(i2c_fd);
	(void)audio_clock_cmd("stop");
	if (!ret)
		printf("fruitjam-wavplay: played %s\n", wav_path);
	return ret;
}
