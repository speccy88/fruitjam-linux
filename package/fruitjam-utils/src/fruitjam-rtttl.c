// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam RP2350 TLV320DAC3100 ringtone player.
 *
 * First audio milestone:
 *   - /dev/fruitjam-audio starts a PIO1 MCLK on GPIO25.
 *   - TLV320DAC3100 derives its PLL from the 15 MHz MCLK.
 *   - RTTTL notes play through a tiny PIO I2S tone stream.
 *
 * This is still a bring-up helper, not an ALSA driver.
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
#define MAX_RTTTL_TEXT 512u
#define AUDIO_SAMPLE_RATE 8000u

enum tone_backend {
	TONE_BACKEND_BEEP,
	TONE_BACKEND_I2S,
};

enum i2s_waveform {
	I2S_WAVE_SINE = 0,
	I2S_WAVE_SQUARE = 1,
	I2S_WAVE_SAW = 2,
	I2S_WAVE_TRIANGLE = 3,
	I2S_WAVE_NOISE = 4,
};

struct builtin_tune {
	const char *name;
	const char *text;
};

static const struct builtin_tune builtin_tunes[] = {
	{ "default", "fruit:d=8,o=5,b=120:c,e,g,c6,g,e,c" },
	{ "scale", "scale:d=8,o=5,b=140:c,d,e,f,g,a,b,c6" },
	{ "startup", "start:d=16,o=5,b=180:c,e,g,c6,8p,g,c6" },
	{ "retro", "retro:d=16,o=5,b=200:c6,g,e,c,g,c6,8p,c6,d6,e6,8g6" },
	{ "chime", "chime:d=8,o=6,b=120:c,e,g,2c7" },
};

static void usage(void)
{
	fprintf(stderr,
		"usage: fruitjam-rtttl [--i2s|--beep] [--loud] [--list|NAME|RTTTL|FILE.rtttl]\n"
		"       fruitjam-rtttl [--i2s|--beep] [--loud] [--waveform WAVE] --tone HZ [MS]\n"
		"       waveforms: sine square saw triangle noise\n"
		"       names: default scale startup retro chime\n");
}

static void list_tunes(void)
{
	size_t i;

	for (i = 0; i < sizeof(builtin_tunes) / sizeof(builtin_tunes[0]); i++)
		printf("%s %s\n", builtin_tunes[i].name, builtin_tunes[i].text);
}

static const char *builtin_tune(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(builtin_tunes) / sizeof(builtin_tunes[0]); i++) {
		if (!strcmp(name, builtin_tunes[i].name))
			return builtin_tunes[i].text;
	}
	return NULL;
}

static void sleep_ms(unsigned int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

static const char *waveform_name(enum i2s_waveform waveform)
{
	switch (waveform) {
	case I2S_WAVE_SINE:
		return "sine";
	case I2S_WAVE_SQUARE:
		return "square";
	case I2S_WAVE_SAW:
		return "saw";
	case I2S_WAVE_TRIANGLE:
		return "triangle";
	case I2S_WAVE_NOISE:
		return "noise";
	default:
		return "sine";
	}
}

static int parse_waveform(const char *text, enum i2s_waveform *waveform)
{
	char *end;
	unsigned long value;

	if (!strcmp(text, "sine")) {
		*waveform = I2S_WAVE_SINE;
		return 0;
	}
	if (!strcmp(text, "square")) {
		*waveform = I2S_WAVE_SQUARE;
		return 0;
	}
	if (!strcmp(text, "saw")) {
		*waveform = I2S_WAVE_SAW;
		return 0;
	}
	if (!strcmp(text, "triangle")) {
		*waveform = I2S_WAVE_TRIANGLE;
		return 0;
	}
	if (!strcmp(text, "noise")) {
		*waveform = I2S_WAVE_NOISE;
		return 0;
	}
	errno = 0;
	value = strtoul(text, &end, 0);
	if (!errno && end && *end == '\0' && value <= I2S_WAVE_NOISE) {
		*waveform = (enum i2s_waveform)value;
		return 0;
	}
	return -1;
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

static int audio_clock_cmd(const char *cmd)
{
	int fd = open("/dev/fruitjam-audio", O_WRONLY);
	size_t len = strlen(cmd);
	ssize_t ret;

	if (fd < 0) {
		fprintf(stderr, "fruitjam-rtttl: open /dev/fruitjam-audio: %s\n",
			strerror(errno));
		return -1;
	}
	ret = write(fd, cmd, len);
	close(fd);
	if (ret != (ssize_t)len) {
		fprintf(stderr, "fruitjam-rtttl: write /dev/fruitjam-audio: %s\n",
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

static int audio_tone(unsigned int hz, unsigned int ms,
		      enum i2s_waveform waveform)
{
	char cmd[32];

	if (!hz) {
		sleep_ms(ms);
		return 0;
	}
	if (ms > 15000u)
		ms = 15000u;
	snprintf(cmd, sizeof(cmd), "tone %u %u %s", hz, ms,
		 waveform_name(waveform));
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

static int i2c_read_reg(int fd, uint8_t page, uint8_t reg)
{
	uint8_t buf[2];
	uint8_t val;

	buf[0] = 0x00;
	buf[1] = page;
	if (write(fd, buf, 2) != 2)
		return -1;
	if (write(fd, &reg, 1) != 1)
		return -1;
	if (read(fd, &val, 1) != 1)
		return -1;
	return val;
}

static int codec_open(void)
{
	int fd = open("/dev/i2c-0", O_RDWR);

	if (fd < 0) {
		perror("fruitjam-rtttl: open /dev/i2c-0");
		return -1;
	}
	if (ioctl(fd, I2C_SLAVE, TLV_ADDR) < 0) {
		perror("fruitjam-rtttl: I2C_SLAVE");
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
		fprintf(stderr, "fruitjam-rtttl: i2c write beep %u Hz\n", hz);
		return -1;
	}
	sleep_ms(ms + 8);
	return 0;
}

static int play_tone(int fd, enum tone_backend backend, unsigned int hz,
		     unsigned int ms, unsigned int beep_volume,
		     enum i2s_waveform waveform)
{
	if (backend == TONE_BACKEND_I2S)
		return audio_tone(hz, ms, waveform);
	return codec_beep_tone(fd, hz, ms, beep_volume);
}

static int codec_init(int fd, int loud, enum tone_backend backend)
{
	uint8_t dac_vol = loud ? 0xe8 : 0xd8; /* -12 dB or -20 dB */
	uint8_t hp_vol = loud ? 0x94 : 0xa8;  /* -10 dB or -20.1 dB */
	uint8_t spk_vol = loud ? 0x8c : 0x94; /* -6 dB or -10 dB */

#define W(page, reg, val)                                                        \
	do {                                                                     \
		if (i2c_write_reg(fd, (page), (reg), (val)) < 0) {               \
			fprintf(stderr, "fruitjam-rtttl: i2c write p%u r0x%02x\n", \
				(unsigned int)(page), (unsigned int)(reg));       \
			return -1;                                                \
		}                                                                \
	} while (0)

	W(0, 0x01, 0x01);       /* software reset */
	sleep_ms(20);
	W(0, 0x3f, 0x14);       /* DACs off, normal paths */
	W(0, 0x05, 0x11);       /* PLL P=1, R=1, off */
	W(0, 0x06, 0x06);       /* PLL J.D=6.9632: 15 MHz -> 104.448 MHz */
	W(0, 0x07, 0x25);
	W(0, 0x08, 0xa0);
	W(0, 0x04, 0x00);       /* PLL input is MCLK */
	W(0, 0x05, 0x91);       /* PLL on */
	sleep_ms(10);
	W(0, 0x04, 0x03);       /* CODEC_CLKIN is PLL */
	W(0, 0x1b, 0x00);       /* I2S, 16-bit stereo, BCLK/WCLK input */
	W(0, 0x0b, 0x91);       /* NDAC on, /17 */
	W(0, 0x0c, 0x81);       /* MDAC on, /1 */
	W(0, 0x0d, 0x03);       /* DOSR 768 -> Fs 8 kHz for kernel I2S tone */
	W(0, 0x0e, 0x00);
	W(0, 0x3c, backend == TONE_BACKEND_BEEP ? 0x19 : 0x01);
	sleep_ms(30);
	W(0, 0x3f, 0xd4);       /* power both DACs, normal paths */
	W(0, 0x40, 0x00);       /* unmute, independent volume */
	W(0, 0x41, dac_vol);    /* left DAC digital volume */
	W(0, 0x42, dac_vol);    /* right DAC digital volume */
	W(1, 0x23, 0x44);       /* left/right DACs to output mixers */
	W(1, 0x1f, 0xd4);       /* power headphone drivers, 1.65 V common */
	W(1, 0x24, hp_vol);     /* route HPL mixer */
	W(1, 0x25, hp_vol);     /* route HPR mixer */
	W(1, 0x26, spk_vol);    /* route mixer to speaker amp */
	W(1, 0x28, 0x34);       /* HPL driver 6 dB, unmute */
	W(1, 0x29, 0x34);       /* HPR driver 6 dB, unmute */
	W(1, 0x2a, 0x0c);       /* speaker driver 12 dB, unmute */
	W(1, 0x20, 0x80);       /* enable class-D speaker amp */
	sleep_ms(350);

#undef W
	return 0;
}

static void codec_print_status(int fd)
{
	int iface = i2c_read_reg(fd, 0, 0x1b);
	int ndac = i2c_read_reg(fd, 0, 0x0b);
	int mdac = i2c_read_reg(fd, 0, 0x0c);
	int dosr_m = i2c_read_reg(fd, 0, 0x0d);
	int dosr_l = i2c_read_reg(fd, 0, 0x0e);
	int prb = i2c_read_reg(fd, 0, 0x3c);
	int dac1 = i2c_read_reg(fd, 0, 0x25);
	int dac2 = i2c_read_reg(fd, 0, 0x26);
	int beep_l = i2c_read_reg(fd, 0, 0x47);
	int beep_r = i2c_read_reg(fd, 0, 0x48);
	int hp = i2c_read_reg(fd, 1, 0x1f);
	int spk = i2c_read_reg(fd, 1, 0x20);
	int route = i2c_read_reg(fd, 1, 0x23);
	int hpl = i2c_read_reg(fd, 1, 0x24);
	int hpr = i2c_read_reg(fd, 1, 0x25);
	int svol = i2c_read_reg(fd, 1, 0x26);
	int hpldrv = i2c_read_reg(fd, 1, 0x28);
	int hprdrv = i2c_read_reg(fd, 1, 0x29);
	int sdrv = i2c_read_reg(fd, 1, 0x2a);

	printf("fruitjam-rtttl: codec status p0.1b=0x%02x "
	       "p0.0b=0x%02x p0.0c=0x%02x "
	       "p0.0d=0x%02x p0.0e=0x%02x p0.3c=0x%02x "
	       "p0.25=0x%02x p0.26=0x%02x p0.47=0x%02x p0.48=0x%02x "
	       "p1.1f=0x%02x p1.20=0x%02x p1.23=0x%02x "
	       "p1.24=0x%02x p1.25=0x%02x p1.26=0x%02x "
	       "p1.28=0x%02x p1.29=0x%02x p1.2a=0x%02x\n",
	       iface, ndac, mdac, dosr_m, dosr_l, prb, dac1, dac2, beep_l,
	       beep_r, hp, spk, route, hpl, hpr, svol, hpldrv, hprdrv, sdrv);
}

static unsigned int note_hz(char name, int sharp, unsigned int octave)
{
	static const unsigned int c4[] = {
		262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
	};
	int idx;
	unsigned int hz;

	switch (name) {
	case 'c': idx = 0; break;
	case 'd': idx = 2; break;
	case 'e': idx = 4; break;
	case 'f': idx = 5; break;
	case 'g': idx = 7; break;
	case 'a': idx = 9; break;
	case 'b': idx = 11; break;
	default: return 0;
	}
	if (sharp)
		idx++;
	if (idx >= 12) {
		idx -= 12;
		octave++;
	}

	hz = c4[idx];
	while (octave > 4) {
		hz *= 2;
		octave--;
	}
	while (octave < 4) {
		hz = (hz + 1) / 2;
		octave++;
	}
	return hz;
}

static const char *parse_uint(const char *p, unsigned int *out)
{
	unsigned long v = 0;
	int any = 0;

	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (unsigned long)(*p - '0');
		p++;
		any = 1;
	}
	if (any)
		*out = (unsigned int)v;
	return p;
}

static int parse_arg_uint(const char *s, unsigned int *out)
{
	const char *end;
	unsigned int value = 0;

	end = parse_uint(s, &value);
	if (end == s || *end)
		return -1;
	*out = value;
	return 0;
}

static int read_rtttl_file(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t ret;
	char *start;
	char *end;

	if (!len)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fruitjam-rtttl: open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	ret = read(fd, buf, len - 1);
	close(fd);
	if (ret < 0) {
		fprintf(stderr, "fruitjam-rtttl: read %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	buf[ret] = '\0';

	start = buf;
	while (*start == ' ' || *start == '\t' ||
	       *start == '\r' || *start == '\n')
		start++;
	end = start;
	while (*end && *end != '\r' && *end != '\n')
		end++;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*end = '\0';
	if (start != buf)
		memmove(buf, start, strlen(start) + 1);
	if (!buf[0]) {
		fprintf(stderr, "fruitjam-rtttl: %s has no RTTTL text\n", path);
		return -1;
	}
	return 0;
}

static int play_rtttl(const char *text, int i2c_fd, enum tone_backend backend,
		      unsigned int beep_volume, enum i2s_waveform waveform)
{
	char copy[MAX_RTTTL_TEXT];
	char *defaults;
	char *notes;
	unsigned int def_dur = 4;
	unsigned int def_oct = 5;
	unsigned int bpm = 120;
	unsigned int whole;
	const char *p;

	if (snprintf(copy, sizeof(copy), "%s", text) >= (int)sizeof(copy)) {
		fprintf(stderr, "fruitjam-rtttl: RTTTL text too long\n");
		return -1;
	}
	defaults = strchr(copy, ':');
	if (!defaults) {
		return -1;
	}
	*defaults++ = '\0';
	notes = strchr(defaults, ':');
	if (!notes) {
		return -1;
	}
	*notes++ = '\0';

	for (p = strtok(defaults, ","); p; p = strtok(NULL, ",")) {
		if (!strncmp(p, "d=", 2))
			def_dur = (unsigned int)strtoul(p + 2, NULL, 10);
		else if (!strncmp(p, "o=", 2))
			def_oct = (unsigned int)strtoul(p + 2, NULL, 10);
		else if (!strncmp(p, "b=", 2))
			bpm = (unsigned int)strtoul(p + 2, NULL, 10);
	}
	if (!def_dur)
		def_dur = 4;
	if (!bpm)
		bpm = 120;
	whole = (60000u * 4u) / bpm;

	for (p = notes; *p;) {
		unsigned int dur = def_dur;
		unsigned int oct = def_oct;
		unsigned int hz;
		unsigned int ms;
		char name;
		int sharp = 0;
		int dotted = 0;

		while (*p == ' ' || *p == ',')
			p++;
		if (!*p)
			break;
		if (*p >= '0' && *p <= '9')
			p = parse_uint(p, &dur);
		name = *p;
		if (name >= 'A' && name <= 'Z')
			name = (char)(name - 'A' + 'a');
		if (!name)
			break;
		p++;
		if (*p == '#') {
			sharp = 1;
			p++;
		}
		if (*p == '.') {
			dotted = 1;
			p++;
		}
		if (*p >= '0' && *p <= '9')
			p = parse_uint(p, &oct);
		if (*p == '.') {
			dotted = 1;
			p++;
		}

		ms = dur ? whole / dur : whole / def_dur;
		if (dotted)
			ms += ms / 2;
		hz = name == 'p' ? 0 : note_hz(name, sharp, oct);
		if (play_tone(i2c_fd, backend, hz, ms ? ms : 1, beep_volume,
			      waveform) < 0) {
			return -1;
		}
		if (backend == TONE_BACKEND_I2S)
			sleep_ms(8);

		while (*p && *p != ',')
			p++;
	}

	return 0;
}

int main(int argc, char **argv)
{
	const char *song = builtin_tune("default");
	const char *song_arg = NULL;
	char file_song[MAX_RTTTL_TEXT];
	unsigned int tone_hz = 0;
	unsigned int tone_ms = 1000;
	unsigned int beep_volume;
	enum i2s_waveform waveform = I2S_WAVE_SINE;
	enum tone_backend backend = TONE_BACKEND_I2S;
	int loud = 0;
	int tone_mode = 0;
	int i2c_fd;
	int argi;
	int ret = 1;

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
		if (!strcmp(argv[argi], "--waveform")) {
			if (++argi >= argc || parse_waveform(argv[argi], &waveform) < 0) {
				usage();
				return 1;
			}
			continue;
		}
		if (!strcmp(argv[argi], "--list") || !strcmp(argv[argi], "list")) {
			list_tunes();
			return 0;
		}
		if (!strcmp(argv[argi], "--tone")) {
			if (++argi >= argc || parse_arg_uint(argv[argi], &tone_hz) < 0) {
				usage();
				return 1;
			}
			if (argi + 1 < argc && argv[argi + 1][0] != '-') {
				if (parse_arg_uint(argv[argi + 1], &tone_ms) < 0) {
					usage();
					return 1;
				}
				argi++;
			}
			tone_mode = 1;
			continue;
		}
		if (song_arg || tone_mode) {
			usage();
			return 1;
		}
		song_arg = argv[argi];
	}

	if (song_arg) {
		song = builtin_tune(song_arg);
		if (!song && !strchr(song_arg, ':')) {
			if (read_rtttl_file(song_arg, file_song, sizeof(file_song)) < 0)
				return 1;
			song = file_song;
		} else if (!song) {
			song = song_arg;
		}
	}
	beep_volume = loud ? 2u : 12u;

	release_peripheral_reset();
	if (audio_clock_cmd("start") < 0)
		return 1;

	i2c_fd = codec_open();
	if (i2c_fd < 0)
		goto out_stop_clock;
	if (codec_init(i2c_fd, loud, backend) < 0)
		goto out_close;
	codec_print_status(i2c_fd);
	if (tone_mode) {
		if (play_tone(i2c_fd, backend, tone_hz, tone_ms, beep_volume,
			      waveform) < 0) {
			fprintf(stderr, "fruitjam-rtttl: failed to play tone\n");
			goto out_close;
		}
		printf("fruitjam-rtttl: played %s %s tone %u Hz %u ms\n",
		       backend == TONE_BACKEND_I2S ? "i2s" : "beep",
		       waveform_name(waveform), tone_hz, tone_ms);
	} else if (play_rtttl(song, i2c_fd, backend, beep_volume,
			      waveform) < 0) {
		fprintf(stderr, "fruitjam-rtttl: failed to play RTTTL\n");
		goto out_close;
	} else {
		printf("fruitjam-rtttl: played %s via %s %s\n", song,
		       backend == TONE_BACKEND_I2S ? "i2s" : "beep",
		       waveform_name(waveform));
	}
	ret = 0;

out_close:
	close(i2c_fd);
out_stop_clock:
	(void)audio_clock_cmd("stop");
	return ret;
}
