// SPDX-License-Identifier: MIT
/*
 * Tiny JSON CGI endpoint for the Fruit Jam hardware playground.
 *
 * The NeoPixel action deliberately generates and runs a Berry script so the
 * web path proves httpd -> CGI -> Berry -> /dev/neopixels.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_PARAMS 24

struct i2c_msg {
	unsigned short addr;
	unsigned short flags;
	unsigned short len;
	unsigned char *buf;
};

struct i2c_rdwr_ioctl_data {
	struct i2c_msg *msgs;
	unsigned int nmsgs;
};

struct param {
	char key[32];
	char value[224];
};

static struct param params[MAX_PARAMS];
static size_t param_count;

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

static void parse_query(void)
{
	const char *qs = getenv("QUERY_STRING");
	const char *p;

	if (!qs)
		return;

	p = qs;
	while (*p && param_count < MAX_PARAMS) {
		const char *amp = strchr(p, '&');
		const char *end = amp ? amp : p + strlen(p);
		const char *eq = memchr(p, '=', (size_t)(end - p));
		size_t key_len;
		size_t value_len;

		if (eq) {
			key_len = (size_t)(eq - p);
			value_len = (size_t)(end - eq - 1);
			url_decode(params[param_count].key, sizeof(params[param_count].key),
				   p, key_len);
			url_decode(params[param_count].value, sizeof(params[param_count].value),
				   eq + 1, value_len);
		} else {
			key_len = (size_t)(end - p);
			url_decode(params[param_count].key, sizeof(params[param_count].key),
				   p, key_len);
			params[param_count].value[0] = '\0';
		}
		param_count++;
		p = amp ? amp + 1 : end;
	}
}

static const char *param(const char *key)
{
	size_t i;

	for (i = 0; i < param_count; i++) {
		if (!strcmp(params[i].key, key))
			return params[i].value;
	}
	return NULL;
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

static int write_text_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	size_t len = strlen(text);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, len);
	close(fd);
	return ret == (ssize_t)len ? 0 : -1;
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

static int run_capture(char *const argv[], char *out, size_t out_len)
{
	char path[64];
	int fd;
	pid_t pid;
	int status = 127;
	ssize_t ret;

	if (out_len)
		out[0] = '\0';
	snprintf(path, sizeof(path), "/tmp/fj-web-%ld.out", (long)getpid());

	fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return 127;

	pid = vfork();
	if (pid < 0) {
		close(fd);
		unlink(path);
		return 127;
	}
	if (pid == 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
		execv(argv[0], argv);
		_exit(127);
	}

	waitpid(pid, &status, 0);
	lseek(fd, 0, SEEK_SET);
	if (out_len) {
		ret = read(fd, out, out_len - 1);
		if (ret < 0)
			out[0] = '\0';
		else
			out[ret] = '\0';
	}
	close(fd);
	unlink(path);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 127;
}

static int parse_color(const char *text, unsigned int rgb[3])
{
	const char *p = text;
	unsigned int values[3];
	size_t i;

	if (!p || !*p)
		p = "000000";
	if (*p == '#')
		p++;
	if (strlen(p) != 6)
		return -1;
	for (i = 0; i < 6; i++) {
		if (hexval(p[i]) < 0)
			return -1;
	}
	values[0] = (unsigned int)((hexval(p[0]) << 4) | hexval(p[1]));
	values[1] = (unsigned int)((hexval(p[2]) << 4) | hexval(p[3]));
	values[2] = (unsigned int)((hexval(p[4]) << 4) | hexval(p[5]));
	rgb[0] = values[0];
	rgb[1] = values[1];
	rgb[2] = values[2];
	return 0;
}

static int appendf(char *buf, size_t len, size_t *used, const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (*used >= len)
		return -1;
	va_start(ap, fmt);
	ret = vsnprintf(buf + *used, len - *used, fmt, ap);
	va_end(ap);
	if (ret < 0 || (size_t)ret >= len - *used)
		return -1;
	*used += (size_t)ret;
	return 0;
}

static void action_neopixels(void)
{
	unsigned int rgb[5][3];
	char script[2048];
	char script_path[64];
	char output[512];
	size_t used = 0;
	size_t i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(rgb); i++) {
		char key[4];
		snprintf(key, sizeof(key), "c%u", (unsigned int)i);
		if (parse_color(param(key), rgb[i]) < 0) {
			json_error("bad color; use RRGGBB or #RRGGBB");
			return;
		}
	}

	snprintf(script_path, sizeof(script_path), "/tmp/fj-web-%ld.be", (long)getpid());
	appendf(script, sizeof(script), &used, "var dev = \"/dev/neopixels\"\nvar f = nil\n");
	appendf(script, sizeof(script), &used,
		"def send(f, cmd)\n  f.write(cmd + \"\\n\")\n  f.flush()\nend\n");
	appendf(script, sizeof(script), &used, "try\n  f = open(dev, \"r+\")\n");
	appendf(script, sizeof(script), &used, "  send(f, \"clear\")\n");
	for (i = 0; i < ARRAY_SIZE(rgb); i++) {
		appendf(script, sizeof(script), &used,
			"  send(f, \"set %u %u %u %u\")\n",
			(unsigned int)i, rgb[i][0], rgb[i][1], rgb[i][2]);
	}
	appendf(script, sizeof(script), &used,
		"  send(f, \"write\")\n  f.close()\n"
		"  print(\"{\\\"ok\\\":true,\\\"source\\\":\\\"berry\\\","
		"\\\"message\\\":\\\"neopixels updated\\\"}\")\n"
		"except .. as e, v\n"
		"  print(\"{\\\"ok\\\":false,\\\"source\\\":\\\"berry\\\","
		"\\\"error\\\":\\\"neopixel write failed\\\"}\")\n"
		"end\n");

	if (used >= sizeof(script) || write_text_file(script_path, script) < 0) {
		json_error("could not write berry script");
		return;
	}

	{
		char *const argv[] = { "/usr/bin/berry", script_path, NULL };
		ret = run_capture(argv, output, sizeof(output));
	}
	unlink(script_path);

	if (ret == 0 && output[0] == '{')
		fputs(output, stdout);
	else {
		printf("{\"ok\":false,\"source\":\"berry\",\"exit\":%d,\"output\":", ret);
		json_string(output);
		puts("}");
	}
}

static int gpio_export(unsigned int gpio)
{
	char text[12];

	snprintf(text, sizeof(text), "%u", gpio);
	return write_existing_file("/sys/class/gpio/export", text);
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

static void print_button_json(const char *name, unsigned int gpio)
{
	int value = gpio_value(gpio);

	printf("{\"name\":");
	json_string(name);
	printf(",\"gpio\":%u,\"value\":%d,\"pressed\":%s}",
	       gpio, value, value == 0 ? "true" : "false");
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

		if (sscanf(line, "%63s %63s", dev, mnt) == 2 &&
		    !strcmp(mnt, mountpoint)) {
			found = 1;
			break;
		}
	}
	fclose(fp);
	return found;
}

static void action_status(void)
{
	char berry_out[64];
	char *const berry_argv[] = { "/usr/bin/berry", "-e", "print(\"berry-ok\")", NULL };
	int berry_ret = run_capture(berry_argv, berry_out, sizeof(berry_out));

	printf("{\"ok\":true");
	printf(",\"berry\":{\"ok\":%s,\"output\":",
	       berry_ret == 0 ? "true" : "false");
	json_string(berry_out);
	printf("}");
	printf(",\"devices\":{\"neopixels\":%s,\"audio\":%s,\"i2c0\":%s,\"sd\":%s}",
	       access("/dev/neopixels", W_OK) == 0 ? "true" : "false",
	       access("/dev/fruitjam-audio", W_OK) == 0 ? "true" : "false",
	       access("/dev/i2c-0", R_OK | W_OK) == 0 ? "true" : "false",
	       is_mounted("/mnt/sd") ? "true" : "false");
	printf(",\"buttons\":[");
	print_button_json("button1", 0);
	putchar(',');
	print_button_json("button2", 4);
	putchar(',');
	print_button_json("button3", 5);
	printf("]}");
}

static int i2c_ping_addr(int fd, int addr)
{
	unsigned char dummy = 0;
	struct i2c_msg msg = {
		.addr = (unsigned short)addr,
		.flags = 0,
		.len = 0,
		.buf = &dummy,
	};
	struct i2c_rdwr_ioctl_data data = {
		.msgs = &msg,
		.nmsgs = 1,
	};

	return ioctl(fd, I2C_RDWR, &data) == 1 ? 0 : -1;
}

static void action_i2c(void)
{
	int fd = open("/dev/i2c-0", O_RDWR);
	int addr;
	int first = 1;

	if (fd < 0) {
		json_error("cannot open /dev/i2c-0");
		return;
	}

	printf("{\"ok\":true,\"bus\":\"/dev/i2c-0\",\"devices\":[");
	for (addr = 0x03; addr <= 0x77; addr++) {
		if (i2c_ping_addr(fd, addr) == 0) {
			if (!first)
				putchar(',');
			printf("\"0x%02x\"", addr);
			first = 0;
		}
	}
	close(fd);
	puts("]}");
}

static int valid_simple_arg(const char *s, size_t max_len, const char *allowed)
{
	size_t i;

	if (!s || strlen(s) > max_len)
		return 0;
	for (i = 0; s[i]; i++) {
		if (!strchr(allowed, s[i]))
			return 0;
	}
	return 1;
}

static void action_adc(void)
{
	const char *channel = param("channel");
	char output[256];
	int ret;

	if (!channel || !*channel)
		channel = "0";
	if (!valid_simple_arg(channel, 8, "0123456789temp")) {
		json_error("bad adc channel");
		return;
	}
	{
		char *const argv[] = {
			"/usr/bin/fruitjam-adc", "read", (char *)channel, NULL
		};
		ret = run_capture(argv, output, sizeof(output));
	}
	printf("{\"ok\":%s,\"exit\":%d,\"output\":",
	       ret == 0 ? "true" : "false", ret);
	json_string(output);
	puts("}");
}

static void action_rtttl(void)
{
	const char *song = param("song");
	char output[512];
	int ret;

	if (song && *song &&
	    !valid_simple_arg(song, 180,
			      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			      "abcdefghijklmnopqrstuvwxyz"
			      "0123456789:=,#. -_")) {
		json_error("bad RTTTL text");
		return;
	}

	if (song && *song) {
		char *const argv[] = {
			"/usr/bin/fruitjam-rtttl", (char *)song, NULL
		};
		ret = run_capture(argv, output, sizeof(output));
	} else {
		char *const argv[] = { "/usr/bin/fruitjam-rtttl", NULL };
		ret = run_capture(argv, output, sizeof(output));
	}

	printf("{\"ok\":%s,\"exit\":%d,\"output\":",
	       ret == 0 ? "true" : "false", ret);
	json_string(output);
	puts("}");
}

static void action_button_test(void)
{
	const char *button = param("button");
	char output[256];
	int ret;

	if (!button || (strcmp(button, "button1") && strcmp(button, "button2") &&
			(strcmp(button, "button3")))) {
		json_error("bad button name");
		return;
	}
	{
		char *const argv[] = {
			"/usr/bin/fruitjam-buttons", "test", (char *)button, NULL
		};
		ret = run_capture(argv, output, sizeof(output));
	}
	printf("{\"ok\":%s,\"exit\":%d,\"output\":",
	       ret == 0 ? "true" : "false", ret);
	json_string(output);
	puts("}");
}

int main(void)
{
	const char *action;

	json_header();
	parse_query();
	action = param("action");
	if (!action || !*action || !strcmp(action, "status"))
		action_status();
	else if (!strcmp(action, "neopixels"))
		action_neopixels();
	else if (!strcmp(action, "i2c"))
		action_i2c();
	else if (!strcmp(action, "adc"))
		action_adc();
	else if (!strcmp(action, "rtttl"))
		action_rtttl();
	else if (!strcmp(action, "button-test"))
		action_button_test();
	else
		json_error("unknown action");

	return 0;
}
