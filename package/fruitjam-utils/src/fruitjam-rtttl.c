// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam RP2350 TLV320DAC3100 ringtone player.
 *
 * First audio milestone:
 *   - /dev/fruitjam-audio starts a PIO1 BCLK/WS clock on GPIO26/GPIO27.
 *   - TLV320DAC3100 derives its PLL from BCLK.
 *   - RTTTL notes play through the TLV320 beep generator and speaker path.
 *
 * This is still a bring-up helper, not an ALSA driver.
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
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
#define SAMPLE_RATE 16000u
static void usage(void)
{
	fprintf(stderr,
		"usage: fruitjam-rtttl [RTTTL]\n"
		"       default RTTTL: fruit:d=8,o=5,b=120:c,e,g,c6,g,e,c\n");
}

static void sleep_ms(unsigned int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
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

static int codec_init(int fd)
{
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
	W(0, 0x1b, 0x00);       /* I2S, 16-bit, BCLK/WCLK input */
	W(0, 0x04, 0x07);       /* CODEC_CLKIN=PLL, PLL_CLKIN=BCLK */
	W(0, 0x05, 0x14);       /* PLL P=1, R=4, off */
	W(0, 0x06, 0x30);       /* PLL J=48: 512 kHz BCLK -> 98.304 MHz */
	W(0, 0x07, 0x00);
	W(0, 0x08, 0x00);
	W(0, 0x0b, 0x86);       /* NDAC on, /6 */
	W(0, 0x0c, 0x88);       /* MDAC on, /8 */
	W(0, 0x0d, 0x00);       /* DOSR 128 -> Fs 16 kHz */
	W(0, 0x0e, 0x80);
	W(0, 0x05, 0x94);       /* PLL on */
	sleep_ms(30);
	W(0, 0x3f, 0xd4);       /* power both DACs, normal paths */
	W(0, 0x40, 0x00);       /* unmute, independent volume */
	W(0, 0x41, 0xd0);       /* left DAC -24 dB */
	W(0, 0x42, 0xd0);       /* right DAC -24 dB */
	W(1, 0x23, 0x44);       /* left/right DACs to output mixers */
	W(1, 0x26, 0x80);       /* route mixer to speaker amp, low analog gain */
	W(1, 0x2a, 0x04);       /* speaker driver 6 dB, unmute */
	W(1, 0x20, 0x80);       /* enable class-D speaker amp */
	W(0, 0x47, 0x1a);       /* beep left -24 dB, disabled */
	W(0, 0x48, 0x5a);       /* beep right matches left */

#undef W
	return 0;
}

static int codec_beep(int fd, unsigned int hz, unsigned int ms)
{
	double angle;
	int sin_val;
	int cos_val;
	uint32_t samples;

	if (!hz) {
		sleep_ms(ms);
		return 0;
	}
	if (hz >= SAMPLE_RATE / 4u)
		hz = SAMPLE_RATE / 4u - 1u;

	angle = 2.0 * M_PI * (double)hz / (double)SAMPLE_RATE;
	sin_val = (int)(sin(angle) * 32767.0 + (sin(angle) >= 0.0 ? 0.5 : -0.5));
	cos_val = (int)(cos(angle) * 32767.0 + (cos(angle) >= 0.0 ? 0.5 : -0.5));
	samples = ((uint32_t)ms * SAMPLE_RATE) / 1000u;
	if (samples > 0x00ffffffu)
		samples = 0x00ffffffu;

	if (i2c_write_reg(fd, 0, 0x49, (samples >> 16) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4a, (samples >> 8) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4b, samples & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4c, ((uint16_t)sin_val >> 8) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4d, (uint16_t)sin_val & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4e, ((uint16_t)cos_val >> 8) & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x4f, (uint16_t)cos_val & 0xff) < 0 ||
	    i2c_write_reg(fd, 0, 0x47, 0x9a) < 0)
		return -1;

	sleep_ms(ms + 5);
	return i2c_write_reg(fd, 0, 0x47, 0x1a);
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

static int play_rtttl(int fd, const char *text)
{
	char *copy = strdup(text);
	char *defaults;
	char *notes;
	unsigned int def_dur = 4;
	unsigned int def_oct = 5;
	unsigned int bpm = 120;
	unsigned int whole;
	const char *p;

	if (!copy)
		return -1;
	defaults = strchr(copy, ':');
	if (!defaults) {
		free(copy);
		return -1;
	}
	*defaults++ = '\0';
	notes = strchr(defaults, ':');
	if (!notes) {
		free(copy);
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
		if (codec_beep(fd, hz, ms ? ms : 1) < 0) {
			free(copy);
			return -1;
		}
		sleep_ms(8);

		while (*p && *p != ',')
			p++;
	}

	free(copy);
	return 0;
}

int main(int argc, char **argv)
{
	const char *song = "fruit:d=8,o=5,b=120:c,e,g,c6,g,e,c";
	int i2c_fd;

	if (argc > 2) {
		usage();
		return 1;
	}
	if (argc == 2) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage();
			return 0;
		}
		song = argv[1];
	}

	if (audio_clock_cmd("start") < 0)
		return 1;

	i2c_fd = codec_open();
	if (i2c_fd < 0)
		return 1;
	if (codec_init(i2c_fd) < 0)
		return 1;
	if (play_rtttl(i2c_fd, song) < 0) {
		fprintf(stderr, "fruitjam-rtttl: failed to play RTTTL\n");
		return 1;
	}
	(void)audio_clock_cmd("stop");
	close(i2c_fd);
	printf("fruitjam-rtttl: played %s\n", song);
	return 0;
}
