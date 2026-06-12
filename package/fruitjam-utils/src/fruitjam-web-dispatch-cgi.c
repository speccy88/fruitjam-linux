// SPDX-License-Identifier: MIT
/*
 * Tiny BusyBox-httpd CGI endpoint for Fruit Jam.
 *
 * Keep this binary small: BusyBox httpd execs it on a no-MMU system, so the
 * flat-binary RAM allocation must stay comfortably below the larger applets.
 * The AirLift HTTP bridge carries the richer dashboard hardware API.
 */

#define _DEFAULT_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BERRY_JSON_BIN "/usr/bin/fruitjam-berry-json"
#define BERRY_DIR "/root/berry"
#define WAV_DIR "/mnt/sd/wavs"
#define BUTTON_FIFO "/run/fruitjam-buttons.fifo"
#define BERRY_SCRIPT_MAX 63
#define WAV_FILE_MAX 95
#define WAV_LIST_MAX 32

static const char *const berry_scripts[] = {
	"00-hello.be",
	"01-language-tour.be",
	"02-files-and-sd.be",
	"03-buttons.be",
	"04-adc-summary.be",
	"05-usbhost-status.be",
	"neopixels.be",
	"neopixel-colors.be",
	"neopixel-rainbow-10s.be",
	"run-all.be",
	"run-visual.be",
};

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
	size_t di = 0;
	size_t si;

	if (!dst_len)
		return;
	for (si = 0; si < src_len && di + 1 < dst_len; si++) {
		if (src[si] == '+') {
			dst[di++] = ' ';
		} else if (src[si] == '%' && si + 2 < src_len) {
			int hi = hexval(src[si + 1]);
			int lo = hexval(src[si + 2]);

			if (hi >= 0 && lo >= 0) {
				dst[di++] = (char)((hi << 4) | lo);
				si += 2;
			} else {
				dst[di++] = src[si];
			}
		} else {
			dst[di++] = src[si];
		}
	}
	dst[di] = '\0';
}

static int query_param(const char *key, char *out, size_t out_len)
{
	const char *p = getenv("QUERY_STRING");
	size_t want = strlen(key);

	if (out_len)
		out[0] = '\0';
	if (!p)
		return 0;
	while (*p) {
		const char *amp = strchr(p, '&');
		const char *end = amp ? amp : p + strlen(p);
		const char *eq = memchr(p, '=', (size_t)(end - p));
		size_t key_len = eq ? (size_t)(eq - p) : (size_t)(end - p);

		if (key_len == want && !strncmp(p, key, want)) {
			if (eq)
				url_decode(out, out_len, eq + 1,
					   (size_t)(end - eq - 1));
			return 1;
		}
		p = amp ? amp + 1 : end;
	}
	return 0;
}

static void json_string(const char *s)
{
	putchar('"');
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;

		switch (c) {
		case '\\':
			fputs("\\\\", stdout);
			break;
		case '"':
			fputs("\\\"", stdout);
			break;
		case '\n':
			fputs("\\n", stdout);
			break;
		case '\r':
			fputs("\\r", stdout);
			break;
		case '\t':
			fputs("\\t", stdout);
			break;
		default:
			if (c < 0x20)
				printf("\\u%04x", c);
			else
				putchar(c);
			break;
		}
	}
	putchar('"');
}

static void json_error(const char *message)
{
	printf("{\"ok\":false,\"error\":");
	json_string(message);
	puts("}");
}

static void json_header(void)
{
	puts("Content-Type: application/json");
	puts("Access-Control-Allow-Origin: *");
	puts("Cache-Control: no-store");
	putchar('\n');
}

static int read_text_file(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t ret;
	size_t n;

	if (!len)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		buf[0] = '\0';
		return -1;
	}
	ret = read(fd, buf, len - 1);
	close(fd);
	if (ret < 0) {
		buf[0] = '\0';
		return -1;
	}
	n = (size_t)ret;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
		     buf[n - 1] == ' ' || buf[n - 1] == '\t'))
		n--;
	buf[n] = '\0';
	return 0;
}

static int write_existing_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	size_t len = strlen(text);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, len);
	close(fd);
	return ret == (ssize_t)len ? 0 : -1;
}

static int gpio_export(unsigned int gpio)
{
	char path[64];
	char text[12];
	int i;

	snprintf(text, sizeof(text), "%u", gpio);
	if (write_existing_file("/sys/class/gpio/export", text) < 0 && errno != EBUSY)
		return -1;
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	for (i = 0; i < 50; i++) {
		if (access(path, F_OK) == 0)
			return 0;
		usleep(2000);
	}
	return -1;
}

static int gpio_value(unsigned int gpio)
{
	char path[64];
	char value[8];

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	if (read_text_file(path, value, sizeof(value)) < 0) {
		gpio_export(gpio);
		if (read_text_file(path, value, sizeof(value)) < 0)
			return -1;
	}
	return value[0] == '0' ? 0 : 1;
}

static int is_mounted(const char *mountpoint)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char line[192];
	int found = 0;

	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		char dev[64];
		char mnt[64];

		(void)dev;
		if (sscanf(line, "%63s %63s", dev, mnt) == 2 &&
		    !strcmp(mnt, mountpoint)) {
			found = 1;
			break;
		}
	}
	fclose(fp);
	return found;
}

static void button_json(const char *name, unsigned int gpio)
{
	int value = gpio_value(gpio);

	printf("{\"name\":");
	json_string(name);
	printf(",\"gpio\":%u,\"value\":%d,\"pressed\":%s}",
	       gpio, value, value == 0 ? "true" : "false");
}

static void action_status(void)
{
	int usbhost_power = gpio_value(11);

	printf("{\"ok\":true,\"control\":{\"ok\":true,\"mode\":\"direct-cgi-tiny\",\"output\":\"tiny C hardware status\"}");
	printf(",\"devices\":{\"neopixels\":%s,\"audio\":%s,\"i2c0\":%s,\"sd\":%s,\"dvi\":%s,\"usbhost\":%s,\"usbhost_power\":%d}",
	       access("/dev/neopixels", W_OK) == 0 ? "true" : "false",
	       access("/dev/fruitjam-audio", W_OK) == 0 ? "true" : "false",
	       access("/dev/i2c-0", R_OK | W_OK) == 0 ? "true" : "false",
	       is_mounted("/mnt/sd") ? "true" : "false",
	       access("/dev/fruitjam-dvi", W_OK) == 0 ? "true" : "false",
	       usbhost_power >= 0 ? "true" : "false",
	       usbhost_power);
	printf(",\"buttons\":[");
	button_json("button1", 0);
	putchar(',');
	button_json("button2", 4);
	putchar(',');
	button_json("button3", 5);
	puts("]}");
}

static int valid_berry_script(const char *name)
{
	size_t i;
	size_t len;

	if (!name || !*name || name[0] == '.')
		return 0;
	len = strlen(name);
	if (len > BERRY_SCRIPT_MAX || len < 4)
		return 0;
	if (strcmp(name + len - 3, ".be"))
		return 0;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];

		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			return 0;
	}
	return 1;
}

static int known_berry_script(const char *name)
{
	size_t i;

	if (!valid_berry_script(name))
		return 0;
	for (i = 0; i < ARRAY_SIZE(berry_scripts); i++) {
		if (!strcmp(name, berry_scripts[i]))
			return 1;
	}
	return 0;
}

static void action_berry_list(void)
{
	size_t i;

	printf("{\"ok\":true,\"source\":\"direct-cgi-tiny\",\"dir\":\"%s\",\"scripts\":[",
	       BERRY_DIR);
	for (i = 0; i < ARRAY_SIZE(berry_scripts); i++) {
		if (i)
			putchar(',');
		json_string(berry_scripts[i]);
	}
	puts("]}");
}

static void action_berry_run(void)
{
	char script[BERRY_SCRIPT_MAX + 1];

	if (!query_param("script", script, sizeof(script)) ||
	    !known_berry_script(script)) {
		json_error("bad berry script");
		return;
	}
	fflush(stdout);
	execl(BERRY_JSON_BIN, "fruitjam-berry-json", script, (char *)NULL);
	json_error("berry helper exec failed");
}

static int wav_name_has_suffix(const char *name)
{
	size_t len;

	if (!name)
		return 0;
	len = strlen(name);
	if (len < 5)
		return 0;
	name += len - 4;
	return tolower((unsigned char)name[0]) == '.' &&
	       tolower((unsigned char)name[1]) == 'w' &&
	       tolower((unsigned char)name[2]) == 'a' &&
	       tolower((unsigned char)name[3]) == 'v';
}

static int valid_wav_file(const char *name)
{
	size_t i;
	size_t len;

	if (!name || !*name || name[0] == '.')
		return 0;
	len = strlen(name);
	if (len > WAV_FILE_MAX || !wav_name_has_suffix(name))
		return 0;
	if (strstr(name, ".."))
		return 0;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];

		if (c < 0x20 || c >= 0x7f || c == '/' || c == '\\')
			return 0;
	}
	return 1;
}

static int wav_file_path(const char *name, char *path, size_t path_len)
{
	int ret;

	if (!valid_wav_file(name))
		return -1;
	ret = snprintf(path, path_len, "%s/%s", WAV_DIR, name);
	return ret > 0 && (size_t)ret < path_len ? 0 : -1;
}

static void action_wav_list(void)
{
	DIR *dir;
	struct dirent *de;
	unsigned int count = 0;

	dir = opendir(WAV_DIR);
	printf("{\"ok\":%s,\"source\":\"direct-cgi-tiny\",\"dir\":\"%s\",\"files\":[",
	       dir ? "true" : "false", WAV_DIR);
	if (dir) {
		while ((de = readdir(dir)) != NULL && count < WAV_LIST_MAX) {
			char path[sizeof(WAV_DIR) + 1 + WAV_FILE_MAX];
			struct stat st;

			if (!valid_wav_file(de->d_name) ||
			    wav_file_path(de->d_name, path, sizeof(path)) < 0 ||
			    stat(path, &st) < 0 || !S_ISREG(st.st_mode))
				continue;
			if (count)
				putchar(',');
			printf("{\"name\":");
			json_string(de->d_name);
			printf(",\"bytes\":%lu}", (unsigned long)st.st_size);
			count++;
		}
		closedir(dir);
	}
	printf("],\"count\":%u", count);
	if (!dir) {
		printf(",\"error\":");
		json_string("cannot open /mnt/sd/wavs");
	}
	puts("}");
}

int main(void)
{
	char action[32];

	json_header();
	if (!query_param("action", action, sizeof(action)) || !action[0] ||
	    !strcmp(action, "status"))
		action_status();
	else if (!strcmp(action, "berry-list"))
		action_berry_list();
	else if (!strcmp(action, "berry-run"))
		action_berry_run();
	else if (!strcmp(action, "wav-list"))
		action_wav_list();
	else
		json_error("action only available through the AirLift HTTP bridge");
	return 0;
}
