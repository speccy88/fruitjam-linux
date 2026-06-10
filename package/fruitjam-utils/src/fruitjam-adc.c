// SPDX-License-Identifier: MIT
/*
 * Fruit Jam ADC reader.
 *
 * The matching kernel helper exposes RP2350B ADC readings in sysfs because the
 * no-MMU userspace /dev/mem path cannot access the ADC register window.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRUITJAM_ADC_SYSFS_DEFAULT "/sys/bus/platform/devices/400a0000.adc"
#define FRUITJAM_ADC_BASE_GPIO 40u
#define FRUITJAM_ADC_TEMP_CH 8u

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-adc [read [CHANNEL|temp]] [--samples N]\n"
		"\n"
		"CHANNEL 0-7 maps to GPIO40-GPIO47 on RP2350B; channel 0 is Fruit Jam A0.\n");
}

static int parse_channel(const char *arg, unsigned int *channel)
{
	char *end;
	unsigned long value;

	if (!arg) {
		*channel = 0;
		return 0;
	}

	if (!strcmp(arg, "temp")) {
		*channel = FRUITJAM_ADC_TEMP_CH;
		return 0;
	}

	errno = 0;
	value = strtoul(arg, &end, 0);
	if (errno || *end || value > FRUITJAM_ADC_TEMP_CH) {
		fprintf(stderr, "fruitjam-adc: invalid channel '%s'\n", arg);
		return -1;
	}

	*channel = (unsigned int)value;
	return 0;
}

static int parse_samples(int argc, char **argv, int *samples)
{
	int i;

	*samples = 1;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--samples")) {
			char *end;
			long value;

			if (i + 1 >= argc) {
				fprintf(stderr, "fruitjam-adc: --samples needs a value\n");
				return -1;
			}
			errno = 0;
			value = strtol(argv[++i], &end, 0);
			if (errno || *end || value < 1 || value > 1000) {
				fprintf(stderr, "fruitjam-adc: invalid sample count\n");
				return -1;
			}
			*samples = (int)value;
		}
	}
	return 0;
}

static int path_has_attr(const char *dir, const char *attr)
{
	char path[PATH_MAX];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s", dir, attr);
	return stat(path, &st) == 0;
}

static int find_adc_sysfs(char *dir, size_t dir_size, const char *attr)
{
	const char *env = getenv("FRUITJAM_ADC_SYSFS");
	DIR *d;
	struct dirent *de;

	if (env && path_has_attr(env, attr)) {
		snprintf(dir, dir_size, "%s", env);
		return 0;
	}

	if (path_has_attr(FRUITJAM_ADC_SYSFS_DEFAULT, attr)) {
		snprintf(dir, dir_size, "%s", FRUITJAM_ADC_SYSFS_DEFAULT);
		return 0;
	}

	d = opendir("/sys/bus/platform/devices");
	if (!d) {
		fprintf(stderr, "fruitjam-adc: open platform sysfs: %s\n", strerror(errno));
		return -1;
	}

	while ((de = readdir(d))) {
		char candidate[PATH_MAX];

		if (de->d_name[0] == '.')
			continue;
		snprintf(candidate, sizeof(candidate), "/sys/bus/platform/devices/%s",
			 de->d_name);
		if (path_has_attr(candidate, attr)) {
			snprintf(dir, dir_size, "%s", candidate);
			closedir(d);
			return 0;
		}
	}

	closedir(d);
	fprintf(stderr, "fruitjam-adc: ADC sysfs attribute '%s' not found\n", attr);
	return -1;
}

static int read_uint_attr(const char *dir, const char *attr, unsigned int *value)
{
	char path[PATH_MAX];
	char buf[64];
	FILE *fp;
	char *end;
	unsigned long parsed;

	snprintf(path, sizeof(path), "%s/%s", dir, attr);
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "fruitjam-adc: open %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (!fgets(buf, sizeof(buf), fp)) {
		fprintf(stderr, "fruitjam-adc: read %s failed\n", path);
		fclose(fp);
		return -1;
	}
	fclose(fp);

	errno = 0;
	parsed = strtoul(buf, &end, 0);
	if (errno || (end == buf)) {
		fprintf(stderr, "fruitjam-adc: invalid value from %s\n", path);
		return -1;
	}

	*value = (unsigned int)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned int channel = 0;
	int samples;
	int arg = 1;
	int i;
	char raw_attr[16];
	char mv_attr[24];
	char sysfs[PATH_MAX];

	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage(stdout);
		return 0;
	}

	if (argc > 1 && !strcmp(argv[1], "read"))
		arg++;
	if (arg < argc && strncmp(argv[arg], "--", 2) &&
	    parse_channel(argv[arg++], &channel) < 0) {
		usage(stderr);
		return 1;
	}
	if (parse_samples(argc, argv, &samples) < 0) {
		usage(stderr);
		return 1;
	}

	snprintf(raw_attr, sizeof(raw_attr), "raw%u", channel);
	snprintf(mv_attr, sizeof(mv_attr), "millivolts%u", channel);
	if (find_adc_sysfs(sysfs, sizeof(sysfs), raw_attr) < 0)
		return 1;

	for (i = 0; i < samples; i++) {
		unsigned int raw;
		unsigned int mv;

		if (read_uint_attr(sysfs, raw_attr, &raw) < 0 ||
		    read_uint_attr(sysfs, mv_attr, &mv) < 0)
			return 1;

		if (channel == FRUITJAM_ADC_TEMP_CH)
			printf("temp adc%u raw %u millivolts %u\n", channel, raw, mv);
		else
			printf("gpio%u adc%u raw %u millivolts %u\n",
			       FRUITJAM_ADC_BASE_GPIO + channel, channel, raw, mv);
	}

	return 0;
}
