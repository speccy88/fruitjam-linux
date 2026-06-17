// SPDX-License-Identifier: MIT
/*
 * Tiny JSON CGI endpoint for the Fruit Jam hardware playground.
 *
 * Hardware actions use direct tiny C paths or purpose-built shell helpers.
 * Berry is only invoked by the explicit /root/berry example runner action.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/reboot.h>
#include <sys/syscall.h>
#else
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_RESTART2 0xa1b2c3d4
#define SYS_reboot (-1)
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_PARAMS 24
#define BUTTON_FIFO "/run/fruitjam-buttons.fifo"
#define ADC_SYSFS "/sys/bus/platform/devices/400a0000.adc"
#define DVI_DEV "/dev/fruitjam-dvi"
#define DVI_BIN "/usr/bin/fruitjam-dvi"
#define ADC_BASE_GPIO 40u
#define ADC_TEMP_CH 8u
#define BERRY_JSON_BIN "/usr/bin/fruitjam-berry-json"
#define BERRY_DIR "/root/berry"
#define WAV_BIN "/usr/bin/fruitjam-wavplay"
#define WAV_DIR "/mnt/sd/wavs"
#define USBHOST_DEV "/dev/fruitjam-usbhost"
#define USBHOST_RESET_MS 50u
#define USBHOST_POST_RESET_US 100000u
#define BERRY_SCRIPT_MAX 63
#define WAV_FILE_MAX 95
#define WAV_LIST_MAX 64

static const char *const berry_scripts[] = {
	"00-hello.be",
	"01-language-tour.be",
	"02-files-and-sd.be",
	"03-buttons.be",
	"04-adc-summary.be",
	"05-usbhost-status.be",
	"06-fruitjam-module.be",
	"neopixels.be",
	"neopixel-colors.be",
	"neopixel-rainbow-10s.be",
	"run-all.be",
	"run-visual.be",
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

static int run_capture_timeout(char *const argv[], char *out, size_t out_len,
			       unsigned int timeout_ms)
{
	char path[64];
	int fd;
	pid_t pid;
	int status = 127;
	volatile unsigned int waited_ms = 0;
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

	for (;;) {
		pid_t done = waitpid(pid, &status, WNOHANG);

		if (done == pid)
			break;
		if (done < 0) {
			status = 127;
			break;
		}
		if (timeout_ms && waited_ms >= timeout_ms) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			status = 124 << 8;
			break;
		}
		usleep(20000);
		waited_ms += 20;
	}
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

static void action_neopixels(void)
{
	unsigned int rgb[5][3];
	size_t i;
	int fd;

	for (i = 0; i < ARRAY_SIZE(rgb); i++) {
		char key[4];
		snprintf(key, sizeof(key), "c%u", (unsigned int)i);
		if (parse_color(param(key), rgb[i]) < 0) {
			json_error("bad color; use RRGGBB or #RRGGBB");
			return;
		}
	}

	fd = open("/dev/neopixels", O_WRONLY);
	if (fd < 0) {
		json_error("cannot open /dev/neopixels");
		return;
	}
	dprintf(fd, "clear\n");
	for (i = 0; i < ARRAY_SIZE(rgb); i++) {
		dprintf(fd, "set %u %u %u %u\n",
			(unsigned int)i, rgb[i][0], rgb[i][1], rgb[i][2]);
	}
	dprintf(fd, "write\n");
	close(fd);

	puts("{\"ok\":true,\"source\":\"dev-neopixels\",\"message\":\"neopixels updated\"}");
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

static int gpio_set_output(unsigned int gpio, int value)
{
	char path[64];

	gpio_export(gpio);
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", gpio);
	if (write_existing_file(path, "out") < 0)
		return -1;
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	return write_existing_file(path, value ? "1" : "0");
}

static int gpio_set_input(unsigned int gpio)
{
	char path[64];

	gpio_export(gpio);
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", gpio);
	return write_existing_file(path, "in");
}

static int usbhost_bus_reset(void)
{
	int ret = 0;

	if (access(USBHOST_DEV, W_OK) == 0)
		return write_existing_file(USBHOST_DEV, "reset 50");

	if (gpio_set_output(11, 1) < 0)
		ret = -1;
	usleep(USBHOST_POST_RESET_US);
	if (gpio_set_output(1, 0) < 0)
		ret = -1;
	if (gpio_set_output(2, 0) < 0)
		ret = -1;
	usleep(USBHOST_RESET_MS * 1000u);
	if (gpio_set_input(1) < 0)
		ret = -1;
	if (gpio_set_input(2) < 0)
		ret = -1;
	usleep(USBHOST_POST_RESET_US);
	return ret;
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
	int usbhost_power = gpio_value(11);

	printf("{\"ok\":true");
	printf(",\"control\":{\"ok\":true,\"mode\":");
	json_string("direct-cgi");
	printf(",\"output\":");
	json_string("direct C and tiny helper hardware paths");
	printf("}");
	printf(",\"devices\":{\"neopixels\":%s,\"audio\":%s,\"i2c0\":%s,\"sd\":%s,\"dvi\":%s,\"usbhost\":%s,\"usbhost_power\":%d}",
	       access("/dev/neopixels", W_OK) == 0 ? "true" : "false",
	       access("/dev/fruitjam-audio", W_OK) == 0 ? "true" : "false",
	       access("/dev/i2c-0", R_OK | W_OK) == 0 ? "true" : "false",
	       is_mounted("/mnt/sd") ? "true" : "false",
	       access(DVI_DEV, W_OK) == 0 ? "true" : "false",
	       usbhost_power >= 0 ? "true" : "false",
	       usbhost_power);
	printf(",\"buttons\":[");
	print_button_json("button1", 0);
	putchar(',');
	print_button_json("button2", 4);
	putchar(',');
	print_button_json("button3", 5);
	printf("]}");
}

#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif

struct cgi_i2c_msg {
	unsigned short addr;
	unsigned short flags;
	unsigned short len;
	unsigned char *buf;
};

struct cgi_i2c_rdwr {
	struct cgi_i2c_msg *msgs;
	unsigned int nmsgs;
};

static int cgi_i2c_ping(int fd, int addr)
{
	unsigned char dummy = 0;
	struct cgi_i2c_msg msg = { (unsigned short)addr, 0, 0, &dummy };
	struct cgi_i2c_rdwr data = { &msg, 1 };

	return ioctl(fd, I2C_RDWR, &data) == 1 ? 0 : -1;
}

static void action_i2c(void)
{
	int fd = open("/dev/i2c-0", O_RDWR);
	int first = 1;
	int addr;

	printf("{\"ok\":%s,\"bus\":\"/dev/i2c-0\",\"source\":\"direct-cgi\",\"devices\":[",
	       fd >= 0 ? "true" : "false");
	for (addr = 0x03; fd >= 0 && addr <= 0x77; addr++) {
		if (cgi_i2c_ping(fd, addr) != 0)
			continue;
		printf("%s\"0x%02x%s\"", first ? "" : ",", addr,
		       addr == 0x18 ? " TLV320DAC3100" : "");
		first = 0;
	}
	printf("],\"exit\":%d", fd >= 0 ? 0 : 1);
	if (fd >= 0) {
		close(fd);
		printf(",\"message\":");
		json_string("live scan of /dev/i2c-0 (0x18 = onboard audio codec)");
	} else {
		printf(",\"error\":");
		json_string("cannot access /dev/i2c-0");
	}
	puts("}");
}

static void action_dvi(void)
{
	const char *cmd = param("cmd");
	const char *text = param("text");
	static const char *const allowed[] = {
		"start", "show", "on", "stop", "off", "test",
		"pattern", "bars", "clear", "black", "white",
	};
	size_t i;
	int ok = 0;
	int fill_only = 0;
	int ret;
	char output[256];

	if (!cmd || !*cmd)
		cmd = "test";
	if (!strcmp(cmd, "dashboard")) {
		char *const argv[] = { DVI_BIN, "dashboard", NULL };

		ret = run_capture_timeout(argv, output, sizeof(output), 7000);
		printf("{\"ok\":%s,\"source\":\"fruitjam-dvi\",\"cmd\":\"dashboard\",\"exit\":%d,\"output\":",
		       ret == 0 ? "true" : "false", ret);
		json_string(output);
		puts("}");
		return;
	}
	if (!strcmp(cmd, "text")) {
		size_t j;

		if (!text || !*text || strlen(text) > 180) {
			json_error("bad DVI text");
			return;
		}
		for (j = 0; text[j]; j++) {
			unsigned char c = (unsigned char)text[j];

			if (c < 0x20 || c > 0x7e) {
				json_error("bad DVI text");
				return;
			}
		}
		{
			char *const argv[] = { DVI_BIN, "text", (char *)text, NULL };

			ret = run_capture_timeout(argv, output, sizeof(output), 7000);
		}
		printf("{\"ok\":%s,\"source\":\"fruitjam-dvi\",\"cmd\":\"text\",\"exit\":%d,\"output\":",
		       ret == 0 ? "true" : "false", ret);
		json_string(output);
		puts("}");
		return;
	}
	for (i = 0; i < ARRAY_SIZE(allowed); i++) {
		if (!strcmp(cmd, allowed[i])) {
			ok = 1;
			break;
		}
	}
	if (!ok) {
		json_error("bad DVI command");
		return;
	}
	if (write_existing_file(DVI_DEV, cmd) < 0) {
		json_error("cannot write /dev/fruitjam-dvi");
		return;
	}
	fill_only = !strcmp(cmd, "pattern") || !strcmp(cmd, "bars") ||
		    !strcmp(cmd, "clear") || !strcmp(cmd, "black") ||
		    !strcmp(cmd, "white");
	if (fill_only && write_existing_file(DVI_DEV, "show") < 0) {
		json_error("cannot show /dev/fruitjam-dvi");
		return;
	}

	printf("{\"ok\":true,\"source\":\"dev-fruitjam-dvi\",\"cmd\":");
	json_string(cmd);
	puts("}");
}

static const char *usb_device_state(int power, int dp, int dm)
{
	if (power < 0)
		return "unknown";
	if (power == 0)
		return "power-off";
	if (dp < 0 || dm < 0)
		return "unknown";
	if (!dp && !dm)
		return "no-device-or-reset";
	if (dp && !dm)
		return "full-speed-device";
	if (!dp && dm)
		return "low-speed-device";
	return "invalid-both-lines-high";
}

static int status_text_int(const char *text, const char *key, int fallback)
{
	size_t key_len = strlen(key);
	const char *p = text;
	char *end;
	long value;

	while (p && *p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			errno = 0;
			value = strtol(p + key_len + 1, &end, 0);
			if (!errno && end != p + key_len + 1)
				return (int)value;
		}
		if (!line_end)
			break;
		p = line_end + 1;
	}

	return fallback;
}

static void status_text_string(const char *text, const char *key,
			       char *out, size_t out_len)
{
	size_t key_len = strlen(key);
	const char *p = text;

	if (!out_len)
		return;
	out[0] = '\0';
	while (p && *p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			size_t n = line_len - key_len - 1;

			if (n >= out_len)
				n = out_len - 1;
			memcpy(out, p + key_len + 1, n);
			out[n] = '\0';
			return;
		}
		if (!line_end)
			break;
		p = line_end + 1;
	}
}

static void action_usbhost(void)
{
	const char *cmd = param("cmd");
	char bridge_status[1024] = "";
	char last_rx_hex[65] = "";
	int bridge_ok = 0;
	int pio_ready = 0;
	int pio_configured = 0;
	int packets = 0;
	int tx_errors = 0;
	int last_tx_result = 0;
	int last_tx_len = 0;
	int rx_attempts = 0;
	int rx_errors = 0;
	int last_rx_result = 0;
	int last_rx_len = 0;
	int last_rx_pid = 0;
	int power;
	int dp;
	int dm;

	if (!cmd || !*cmd)
		cmd = "status";
	if (!strcmp(cmd, "on")) {
		if (access(USBHOST_DEV, W_OK) == 0) {
			if (write_existing_file(USBHOST_DEV, "on") < 0) {
				json_error("cannot enable USB host power");
				return;
			}
		} else if (gpio_set_output(11, 1) < 0) {
			json_error("cannot enable USB host power");
			return;
		}
	} else if (!strcmp(cmd, "off")) {
		if (access(USBHOST_DEV, W_OK) == 0) {
			if (write_existing_file(USBHOST_DEV, "off") < 0) {
				json_error("cannot disable USB host power");
				return;
			}
		} else if (gpio_set_output(11, 0) < 0) {
			json_error("cannot disable USB host power");
			return;
		}
	} else if (!strcmp(cmd, "in-token") ||
		   !strcmp(cmd, "get-device-8") ||
		   !strcmp(cmd, "get-device-8-combo-skipack") ||
		   !strcmp(cmd, "reset-get-device-8-combo-skipack")) {
		if (access(USBHOST_DEV, W_OK) != 0) {
			json_error("USB host PIO probe requires kernel bridge");
			return;
		}
		(void)write_existing_file(USBHOST_DEV, cmd);
	} else if (!strcmp(cmd, "reset")) {
		if (usbhost_bus_reset() < 0) {
			json_error("cannot reset USB host bus");
			return;
		}
	} else if (strcmp(cmd, "status")) {
		json_error("bad USB host command");
			return;
	}

	if (access(USBHOST_DEV, R_OK) == 0 &&
	    read_text_file(USBHOST_DEV, bridge_status, sizeof(bridge_status)) == 0) {
		bridge_ok = 1;
		power = status_text_int(bridge_status, "power", -1);
		dp = status_text_int(bridge_status, "dp", -1);
		dm = status_text_int(bridge_status, "dm", -1);
		pio_ready = status_text_int(bridge_status, "pio_ready", 0);
		pio_configured = status_text_int(bridge_status, "pio_configured", 0);
		packets = status_text_int(bridge_status, "packets", 0);
		tx_errors = status_text_int(bridge_status, "tx_errors", 0);
		last_tx_result = status_text_int(bridge_status, "last_tx_result", 0);
		last_tx_len = status_text_int(bridge_status, "last_tx_len", 0);
		rx_attempts = status_text_int(bridge_status, "rx_attempts", 0);
		rx_errors = status_text_int(bridge_status, "rx_errors", 0);
		last_rx_result = status_text_int(bridge_status, "last_rx_result", 0);
		last_rx_len = status_text_int(bridge_status, "last_rx_len", 0);
		last_rx_pid = status_text_int(bridge_status, "last_rx_pid", 0);
		status_text_string(bridge_status, "last_rx_hex", last_rx_hex,
				   sizeof(last_rx_hex));
	} else {
		power = gpio_value(11);
		dp = gpio_value(1);
		dm = gpio_value(2);
	}
	printf("{\"ok\":%s,\"source\":\"direct-cgi-gpio\",\"cmd\":",
	       power >= 0 ? "true" : "false");
	json_string(cmd);
	printf(",\"power\":%d,\"dp\":%d,\"dm\":%d,\"stack\":",
	       power, dp, dm);
	json_string(access(USBHOST_DEV, R_OK) == 0 ?
		    "kernel bridge line-state; PIO2 host program staged; HID report polling not implemented yet" :
		    "sysfs line-state only; PIO USB host/HID report polling not implemented yet");
	printf(",\"present\":%s,\"hid\":false,\"driver\":\"%s\",\"pio_ready\":%s,\"pio_configured\":%s,\"packets\":%d,\"tx_errors\":%d,\"last_tx_result\":%d,\"last_tx_len\":%d,\"rx_attempts\":%d,\"rx_errors\":%d,\"last_rx_result\":%d,\"last_rx_len\":%d,\"last_rx_pid\":%d,\"last_rx_hex\":",
	       power > 0 && ((dp == 1 && dm == 0) || (dp == 0 && dm == 1)) ?
	       "true" : "false",
	       bridge_ok ? "kernel-line-state" : "sysfs-line-state",
	       pio_ready ? "true" : "false",
	       pio_configured ? "true" : "false",
	       packets, tx_errors, last_tx_result, last_tx_len,
	       rx_attempts, rx_errors, last_rx_result, last_rx_len,
	       last_rx_pid);
	json_string(last_rx_hex);
	printf(",\"probe_ok\":%s,\"next\":\"pio-packet-io\",\"first_milestone\":\"boot-protocol-keyboard\"",
	       rx_attempts > 0 && last_rx_result == 0 ? "true" : "false");
	if (!strcmp(cmd, "reset"))
		printf(",\"reset_ms\":%u", USBHOST_RESET_MS);
	printf(",\"device\":");
	json_string(usb_device_state(power, dp, dm));
	puts("}");
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

static int parse_uint_limited(const char *s, unsigned int min_value,
			      unsigned int max_value, unsigned int *out)
{
	char *end;
	unsigned long parsed;

	if (!s || !*s || !valid_simple_arg(s, 10, "0123456789"))
		return -1;
	errno = 0;
	parsed = strtoul(s, &end, 10);
	if (errno || *end || parsed < min_value || parsed > max_value)
		return -1;
	*out = (unsigned int)parsed;
	return 0;
}

static int reboot_bootsel_after_delay(unsigned int delay_ms)
{
	if (delay_ms)
		usleep(delay_ms * 1000u);
	sync();
	return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		       LINUX_REBOOT_CMD_RESTART2, "bootsel");
}

static void drop_page_cache(void)
{
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);

	if (fd < 0)
		return;
	write(fd, "3\n", 2);
	close(fd);
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

	printf("{\"ok\":true,\"source\":\"direct-cgi\",\"dir\":\"%s\",\"scripts\":[",
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
	const char *script = param("script");

	if (!known_berry_script(script)) {
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
	printf("{\"ok\":%s,\"source\":\"direct-cgi\",\"dir\":\"%s\",\"files\":[",
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

static void action_wav_play(void)
{
	const char *file = param("file");
	const char *backend = param("backend");
	const char *loud = param("loud");
	char path[sizeof(WAV_DIR) + 1 + WAV_FILE_MAX];
	char output[768];
	int use_beep = backend && !strcmp(backend, "beep");
	int ret;

	if (wav_file_path(file, path, sizeof(path)) < 0) {
		json_error("bad WAV file");
		return;
	}
	if (access(path, R_OK) < 0) {
		json_error("WAV file not found");
		return;
	}
	drop_page_cache();
	if (loud && !strcmp(loud, "0")) {
		char *const argv[] = {
			WAV_BIN, use_beep ? "--beep" : "--i2s", path, NULL
		};
		ret = run_capture_timeout(argv, output, sizeof(output), 50000);
	} else {
		char *const argv[] = {
			WAV_BIN, use_beep ? "--beep" : "--i2s", "--loud", path, NULL
		};
		ret = run_capture_timeout(argv, output, sizeof(output), 50000);
	}
	printf("{\"ok\":%s,\"exit\":%d,\"source\":\"direct-cgi-wav\",\"file\":",
	       ret == 0 ? "true" : "false", ret);
	json_string(file);
	printf(",\"backend\":\"%s\",\"output\":", use_beep ? "beep" : "i2s");
	json_string(output);
	puts("}");
}

static void action_adc(void)
{
	const char *channel = param("channel");
	char raw_attr[96];
	char mv_attr[96];
	char output[128];
	unsigned int raw;
	unsigned int mv;
	unsigned int ch;
	char *end;
	unsigned long parsed;

	if (!channel || !*channel)
		channel = "0";
	if (!valid_simple_arg(channel, 8, "0123456789temp")) {
		json_error("bad adc channel");
		return;
	}
	if (!strcmp(channel, "temp")) {
		ch = ADC_TEMP_CH;
	} else {
		errno = 0;
		parsed = strtoul(channel, &end, 0);
		if (errno || *end || parsed > ADC_TEMP_CH) {
			json_error("bad adc channel");
			return;
		}
		ch = (unsigned int)parsed;
	}

	snprintf(raw_attr, sizeof(raw_attr), "%s/raw%u", ADC_SYSFS, ch);
	snprintf(mv_attr, sizeof(mv_attr), "%s/millivolts%u", ADC_SYSFS, ch);
	if (read_text_file(raw_attr, output, sizeof(output)) < 0) {
		json_error("ADC raw sysfs attribute not available");
		return;
	}
	errno = 0;
	parsed = strtoul(output, &end, 0);
	if (errno || end == output) {
		json_error("ADC raw sysfs value is invalid");
		return;
	}
	raw = (unsigned int)parsed;

	if (read_text_file(mv_attr, output, sizeof(output)) < 0) {
		json_error("ADC millivolts sysfs attribute not available");
		return;
	}
	errno = 0;
	parsed = strtoul(output, &end, 0);
	if (errno || end == output) {
		json_error("ADC millivolts sysfs value is invalid");
		return;
	}
	mv = (unsigned int)parsed;

	if (ch == ADC_TEMP_CH)
		snprintf(output, sizeof(output), "temp adc%u raw %u millivolts %u\n",
			 ch, raw, mv);
	else
		snprintf(output, sizeof(output), "gpio%u adc%u raw %u millivolts %u\n",
			 ADC_BASE_GPIO + ch, ch, raw, mv);

	printf("{\"ok\":true,\"exit\":0,\"source\":\"direct-cgi\",\"channel\":\"");
	printf("%s", ch == ADC_TEMP_CH ? "temp" : channel);
	printf("\",\"raw\":%u,\"millivolts\":%u,\"output\":", raw, mv);
	json_string(output);
	puts("}");
}

static void action_rtttl(void)
{
	const char *song = param("song");
	const char *tone = param("tone");
	const char *ms = param("ms");
	char output[512];
	char tone_arg[16];
	char ms_arg[16];
	unsigned int tone_hz;
	unsigned int tone_ms = 1200;
	int ret;

	if (tone && *tone) {
		if (parse_uint_limited(tone, 20, 8000, &tone_hz) < 0 ||
		    (ms && *ms && parse_uint_limited(ms, 20, 10000, &tone_ms) < 0)) {
			json_error("bad tone parameters");
			return;
		}
		snprintf(tone_arg, sizeof(tone_arg), "%u", tone_hz);
		snprintf(ms_arg, sizeof(ms_arg), "%u", tone_ms);
		{
			char *const argv[] = {
				"/usr/bin/fruitjam-rtttl", "--tone",
				tone_arg, ms_arg, NULL
			};
			ret = run_capture_timeout(argv, output, sizeof(output),
						  tone_ms + 3500);
		}
		printf("{\"ok\":%s,\"exit\":%d,\"mode\":\"tone\",\"tone\":%u,\"ms\":%u,\"output\":",
		       ret == 0 ? "true" : "false", ret, tone_hz, tone_ms);
		json_string(output);
		puts("}");
		return;
	}

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
		ret = run_capture_timeout(argv, output, sizeof(output), 5000);
	} else {
		char *const argv[] = { "/usr/bin/fruitjam-rtttl", NULL };
		ret = run_capture_timeout(argv, output, sizeof(output), 5000);
	}

	printf("{\"ok\":%s,\"exit\":%d,\"output\":",
	       ret == 0 ? "true" : "false", ret);
	json_string(output);
	puts("}");
}

static void action_bootsel(void)
{
	puts("{\"ok\":true,\"accepted\":true,\"verified\":false,"
	     "\"source\":\"direct-cgi\","
	     "\"message\":\"BOOTSEL request accepted; verify from host with picotool info -a\"}");
	fflush(stdout);
	if (reboot_bootsel_after_delay(1200) < 0)
		fprintf(stderr, "fruitjam-web-cgi: reboot bootsel: %s\n",
			strerror(errno));
}

static void action_button_test(void)
{
	const char *button = param("button");
	char line[32];
	char output[256];
	int fd;
	int ret;

	if (!button || (strcmp(button, "button1") && strcmp(button, "button2") &&
			(strcmp(button, "button3")))) {
		json_error("bad button name");
		return;
	}

	snprintf(line, sizeof(line), "test %s\n", button);
	fd = open(BUTTON_FIFO, O_WRONLY | O_NONBLOCK);
	if (fd >= 0) {
		ssize_t written = write(fd, line, strlen(line));

		close(fd);
		if (written == (ssize_t)strlen(line)) {
			printf("{\"ok\":true,\"source\":\"fifo\",\"output\":");
			json_string(line);
			puts("}");
			return;
		}
	}

	{
		char *const argv[] = {
			"/usr/bin/fruitjam-buttons", "test", (char *)button, NULL
		};
		ret = run_capture_timeout(argv, output, sizeof(output), 1500);
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
	else if (!strcmp(action, "dvi"))
		action_dvi();
	else if (!strcmp(action, "usbhost"))
		action_usbhost();
	else if (!strcmp(action, "adc"))
		action_adc();
	else if (!strcmp(action, "rtttl"))
		action_rtttl();
	else if (!strcmp(action, "button-test"))
		action_button_test();
	else if (!strcmp(action, "berry-list"))
		action_berry_list();
	else if (!strcmp(action, "berry-run"))
		action_berry_run();
	else if (!strcmp(action, "wav-list"))
		action_wav_list();
	else if (!strcmp(action, "wav-play"))
		action_wav_play();
	else if (!strcmp(action, "bootsel"))
		action_bootsel();
	else
		json_error("unknown action");

	return 0;
}
