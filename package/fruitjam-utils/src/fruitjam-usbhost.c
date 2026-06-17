// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam USB host power/status helper.
 *
 * Fruit Jam routes USB host D+/D- through GPIO1/GPIO2 and controls host 5 V
 * power with GPIO11. This helper manages power and reports line-level
 * readiness for the future PIO USB HID bridge; it is not the protocol stack.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef GPIO_ROOT
#define GPIO_ROOT "/sys/class/gpio"
#endif
#ifndef BRIDGE_DEV
#define BRIDGE_DEV "/dev/fruitjam-usbhost"
#endif

#define GPIO_DP 1u
#define GPIO_DM 2u
#define GPIO_POWER 11u
#define POLL_US 100000
#define KBD_POLL_US 10000u
#define MAX_SECONDS 600u
#define DEFAULT_KBD_SECONDS 30u
#define DEFAULT_RESET_MS 50u
#define MAX_RESET_MS 1000u
#define POST_RESET_US 100000u
#define GPIO_PATH_MAX 192
#define BRIDGE_BUF_MAX 1024
#define RX_HEX_MAX 65
#define RX_BYTES_MAX 32
#define HID_REPORT_LEN 8
#define HID_KEY_SLOTS 6
#define HID_MOD_LSHIFT 0x02u
#define HID_MOD_RSHIFT 0x20u
#define USB_PID_NAK 0x5au
#define KBD_ADDR_DEFAULT 1u
#define KBD_CONFIG_DEFAULT 1u
#define KBD_IFACE_DEFAULT 0u
#define KBD_EP_DEFAULT 1u
#define KBD_SCAN_CONFIG_MAX 2u
#define KBD_SCAN_IFACE_MAX 3u
#define KBD_SCAN_EP_MAX 4u
#define KBD_SHELL_LINE_SIZE 256
#define KBD_HISTORY_DEPTH 4
#define KBD_CH_RIGHT 0x100
#define KBD_CH_LEFT 0x101
#define KBD_CH_DOWN 0x102
#define KBD_CH_UP 0x103
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct usbhost_status {
	int power;
	int dp;
	int dm;
	int pio_ready;
	int pio_configured;
	int packets;
	int tx_errors;
	int last_tx_result;
	int last_tx_len;
	int rx_attempts;
	int rx_errors;
	int last_rx_result;
	int last_rx_len;
	int last_rx_pid;
	char last_rx_hex[RX_HEX_MAX];
	const char *device;
	int present;
	bool kernel_bridge;
};

static const char *sysfs_stack_text =
	"sysfs line-state only; PIO USB host/HID report polling not implemented yet";
static const char *kernel_stack_text =
	"kernel bridge line-state; PIO USB host/HID report polling not implemented yet";
static const char *kernel_pio_stack_text =
	"kernel bridge line-state; PIO2 host program staged; boot-keyboard init/poll/text/event path available";

struct hid_live_state {
	unsigned char prev[HID_KEY_SLOTS];
};

struct keyboard_target {
	unsigned int addr;
	unsigned int config;
	unsigned int iface;
	unsigned int ep;
};

static const char *path_dirs[] = {
	"/bin", "/usr/bin", "/sbin", "/usr/sbin"
};

static const char *keyboard_builtins[] = {
	"cd", "echo", "exit", "help", "history", "status", "?"
};

static void read_status(struct usbhost_status *st);
static void print_human(const struct usbhost_status *st);

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-usbhost {status|json|decode [RX-HEX]|hid [RX-HEX|REPORT-HEX]|on|off|reset [ms]|pio-init|tx-test|self-rx|sof-burst|in-token|setup-token-self-rx|setup-data-self-rx|setup-data-self-rx-noeop|setup-data-self-rx-cpu|setup-data-self-rx-drain|data-len-sweep|get-device-8|in-token-gated|get-device-8-gated|get-device-8-gated-cpu|get-device-8-combo|get-device-8-combo-skipack|get-device-8-fast|get-device-8-tight|get-device-8-burst|get-device-8-stream|reset-get-device-8|reset-get-device-8-gated|reset-get-device-8-combo|reset-get-device-8-combo-skipack|reset-get-device-8-fast|reset-get-device-8-tight|reset-get-device-8-burst|reset-get-device-8-stream|kbd-init [addr config iface]|kbd-poll [addr ep]|kbd-init-poll [addr config iface ep]|kbd-find|kbd-text [seconds [addr config iface ep]]|kbd-events [seconds [addr config iface ep]]|kbd-monitor [seconds [addr config iface ep]]|kbd-shell [seconds [addr config iface ep]]|kbd-auto-text [seconds]|kbd-auto-events [seconds]|kbd-auto-monitor [seconds]|kbd-auto-shell [seconds]|wait [seconds]|monitor [seconds]}\n");
}

static int write_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, strlen(text));
	close(fd);
	return ret == (ssize_t)strlen(text) ? 0 : -1;
}

static int read_file(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t ret;

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
	buf[ret] = '\0';
	while (ret > 0 && (buf[ret - 1] == '\n' || buf[ret - 1] == '\r' ||
			   buf[ret - 1] == ' ' || buf[ret - 1] == '\t'))
		buf[--ret] = '\0';
	return 0;
}

static int root_path(const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/%s", GPIO_ROOT, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/gpio%u/%s", GPIO_ROOT, gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_export(unsigned int gpio)
{
	char path[GPIO_PATH_MAX];
	char text[12];
	int fd;
	ssize_t ret;
	int i;

	if (gpio_path(gpio, "value", path, sizeof(path)) == 0 &&
	    access(path, F_OK) == 0)
		return 0;
	if (root_path("export", path, sizeof(path)) < 0)
		return -1;
	fd = open(path, O_WRONLY);
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
		usleep(2000);
	}
	return -1;
}

static int gpio_direction(unsigned int gpio, const char *direction)
{
	char path[GPIO_PATH_MAX];

	if (gpio_export(gpio) < 0)
		return -1;
	if (gpio_path(gpio, "direction", path, sizeof(path)) < 0)
		return -1;
	return write_file(path, direction);
}

static int gpio_input(unsigned int gpio)
{
	return gpio_direction(gpio, "in");
}

static int gpio_write(unsigned int gpio, int value)
{
	char path[GPIO_PATH_MAX];

	if (gpio_direction(gpio, "out") < 0)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	return write_file(path, value ? "1" : "0");
}

static int gpio_read(unsigned int gpio)
{
	char path[GPIO_PATH_MAX];
	char text[8];

	if (gpio_export(gpio) < 0)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	if (read_file(path, text, sizeof(text)) < 0)
		return -1;
	return text[0] == '0' ? 0 : 1;
}

static int gpio_read_input(unsigned int gpio)
{
	if (gpio_input(gpio) < 0)
		return -1;
	return gpio_read(gpio);
}

static const char *usb_state(int power, int dp, int dm)
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

static int hex_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_hex_bytes(const char *text, unsigned char *bytes,
			   size_t max_bytes, size_t *byte_count)
{
	size_t n = 0;
	int hi = -1;

	while (*text) {
		int v;

		if (isspace((unsigned char)*text) || *text == ':' ||
		    *text == ',' || *text == '-' || *text == '_') {
			text++;
			continue;
		}
		if (*text == 'x' || *text == 'X') {
			text++;
			continue;
		}
		v = hex_value((unsigned char)*text);
		if (v < 0)
			return -1;
		if (hi < 0) {
			hi = v;
		} else {
			if (n >= max_bytes)
				return -1;
			bytes[n++] = (unsigned char)((hi << 4) | v);
			hi = -1;
		}
		text++;
	}
	if (hi >= 0)
		return -1;
	*byte_count = n;
	return 0;
}

static bool usb_pid_valid(unsigned char pid)
{
	return (((pid >> 4) ^ (pid & 0x0f)) == 0x0f);
}

static const char *usb_pid_name(unsigned char pid)
{
	switch (pid) {
	case 0xe1: return "OUT";
	case 0x69: return "IN";
	case 0xa5: return "SOF";
	case 0x2d: return "SETUP";
	case 0xc3: return "DATA0";
	case 0x4b: return "DATA1";
	case 0x87: return "DATA2";
	case 0x0f: return "MDATA";
	case 0xd2: return "ACK";
	case 0x5a: return "NAK";
	case 0x1e: return "STALL";
	case 0x96: return "NYET";
	case 0x3c: return "PRE/ERR";
	case 0x78: return "SPLIT";
	case 0xb4: return "PING";
	default: return "unknown";
	}
}

static bool usb_pid_is_data(unsigned char pid)
{
	return pid == 0xc3 || pid == 0x4b || pid == 0x87 || pid == 0x0f;
}

static const char *hid_key_name(unsigned char key)
{
	static char name[8];

	if (key >= 4 && key <= 29) {
		name[0] = (char)('a' + key - 4);
		name[1] = '\0';
		return name;
	}
	if (key >= 30 && key <= 38) {
		name[0] = (char)('1' + key - 30);
		name[1] = '\0';
		return name;
	}
	if (key == 39)
		return "0";

	switch (key) {
	case 40: return "enter";
	case 41: return "esc";
	case 42: return "backspace";
	case 43: return "tab";
	case 44: return "space";
	case 45: return "-";
	case 46: return "=";
	case 47: return "[";
	case 48: return "]";
	case 49: return "\\";
	case 50: return "nonus";
	case 51: return ";";
	case 52: return "'";
	case 53: return "`";
	case 54: return ",";
	case 55: return ".";
	case 56: return "/";
	case 57: return "capslock";
	case 58: return "f1";
	case 59: return "f2";
	case 60: return "f3";
	case 61: return "f4";
	case 62: return "f5";
	case 63: return "f6";
	case 64: return "f7";
	case 65: return "f8";
	case 66: return "f9";
	case 67: return "f10";
	case 68: return "f11";
	case 69: return "f12";
	case 79: return "right";
	case 80: return "left";
	case 81: return "down";
	case 82: return "up";
	default:
		snprintf(name, sizeof(name), "0x%02x", key);
		return name;
	}
}

static int hid_key_ascii(unsigned char key, bool shift)
{
	if (key >= 4 && key <= 29)
		return (shift ? 'A' : 'a') + key - 4;
	if (key >= 30 && key <= 38) {
		static const char normal[] = "123456789";
		static const char shifted[] = "!@#$%^&*(";

		return shift ? shifted[key - 30] : normal[key - 30];
	}
	if (key == 39)
		return shift ? ')' : '0';

	switch (key) {
	case 40: return '\n';
	case 41: return '\033';
	case 42: return '\177';
	case 43: return '\t';
	case 44: return ' ';
	case 45: return shift ? '_' : '-';
	case 46: return shift ? '+' : '=';
	case 47: return shift ? '{' : '[';
	case 48: return shift ? '}' : ']';
	case 49: return shift ? '|' : '\\';
	case 51: return shift ? ':' : ';';
	case 52: return shift ? '"' : '\'';
	case 53: return shift ? '~' : '`';
	case 54: return shift ? '<' : ',';
	case 55: return shift ? '>' : '.';
	case 56: return shift ? '?' : '/';
	default: return 0;
	}
}

static int hid_key_shell_code(unsigned char key, bool shift)
{
	switch (key) {
	case 79: return KBD_CH_RIGHT;
	case 80: return KBD_CH_LEFT;
	case 81: return KBD_CH_DOWN;
	case 82: return KBD_CH_UP;
	default: return hid_key_ascii(key, shift);
	}
}

static void print_quoted_text(const char *text)
{
	putchar('"');
	while (*text) {
		if (*text == '"' || *text == '\\')
			putchar('\\');
		putchar(*text++);
	}
	puts("\"");
}

static bool print_hid_keyboard_hint(const unsigned char *data, size_t len,
				    const char *prefix)
{
	char text[HID_KEY_SLOTS + 1];
	size_t text_len = 0;
	bool shift;
	bool any = false;
	size_t i;

	if (len != HID_REPORT_LEN || data[1] != 0)
		return false;

	shift = data[0] & (HID_MOD_LSHIFT | HID_MOD_RSHIFT);
	printf("%s boot-keyboard modifiers=0x%02x keys=", prefix, data[0]);
	for (i = 2; i < HID_REPORT_LEN; i++) {
		unsigned char key = data[i];
		int ch;

		if (key < 4)
			continue;
		if (any)
			putchar(',');
		printf("%s(0x%02x)", hid_key_name(key), key);
		any = true;

		ch = hid_key_ascii(key, shift);
		if (ch >= 32 && ch <= 126 && text_len < HID_KEY_SLOTS)
			text[text_len++] = (char)ch;
	}
	if (!any)
		fputs("none", stdout);
	putchar('\n');

	if (text_len) {
		text[text_len] = '\0';
		printf("%s-text ", prefix);
		print_quoted_text(text);
	}
	return true;
}

static bool hid_key_in_prev(const struct hid_live_state *state,
			    unsigned char key)
{
	size_t i;

	for (i = 0; i < HID_KEY_SLOTS; i++) {
		if (state->prev[i] == key)
			return true;
	}
	return false;
}

static bool hid_key_in_report(const unsigned char report[HID_REPORT_LEN],
			      unsigned char key)
{
	size_t i;

	for (i = 2; i < HID_REPORT_LEN; i++) {
		if (report[i] == key)
			return true;
	}
	return false;
}

static void hid_live_copy_prev(struct hid_live_state *state,
			       const unsigned char report[HID_REPORT_LEN])
{
	size_t i;

	for (i = 0; i < HID_KEY_SLOTS; i++)
		state->prev[i] = report[i + 2];
}

static void print_hid_event(const char *kind, unsigned char key, int ch,
			    unsigned char mod)
{
	if (ch >= 32 && ch <= 126)
		printf("%s key=%s char=%c code=0x%02x modifiers=0x%02x\n",
		       kind, hid_key_name(key), ch, key, mod);
	else if (ch == '\n')
		printf("%s key=%s char=enter code=0x%02x modifiers=0x%02x\n",
		       kind, hid_key_name(key), key, mod);
	else if (ch == '\t')
		printf("%s key=%s char=tab code=0x%02x modifiers=0x%02x\n",
		       kind, hid_key_name(key), key, mod);
	else if (ch == '\177')
		printf("%s key=%s char=backspace code=0x%02x modifiers=0x%02x\n",
		       kind, hid_key_name(key), key, mod);
	else if (ch == '\033')
		printf("%s key=%s char=esc code=0x%02x modifiers=0x%02x\n",
		       kind, hid_key_name(key), key, mod);
	else
		printf("%s key=%s code=0x%02x modifiers=0x%02x\n",
		       kind, hid_key_name(key), key, mod);
}

static void emit_hid_text_char(int ch)
{
	if (ch == '\177') {
		fputs("\b \b", stdout);
		return;
	}
	if (ch == '\033')
		return;
	if (ch == '\n' || ch == '\t' || (ch >= 32 && ch <= 126))
		putchar(ch);
}

static void process_hid_live_report(struct hid_live_state *state,
				    const unsigned char report[HID_REPORT_LEN],
				    bool events)
{
	bool shift = report[0] & (HID_MOD_LSHIFT | HID_MOD_RSHIFT);
	size_t i;

	for (i = 0; i < HID_KEY_SLOTS; i++) {
		unsigned char key = state->prev[i];

		if (key >= 4 && !hid_key_in_report(report, key) && events)
			printf("release key=%s code=0x%02x\n",
			       hid_key_name(key), key);
	}

	for (i = 2; i < HID_REPORT_LEN; i++) {
		unsigned char key = report[i];
		int ch;

		if (key < 4 || hid_key_in_prev(state, key))
			continue;

		ch = hid_key_ascii(key, shift);
		if (events)
			print_hid_event("press", key, ch, report[0]);
		else
			emit_hid_text_char(ch);
	}

	hid_live_copy_prev(state, report);
	if (!events)
		fflush(stdout);
}

static size_t collect_hid_text_chars(struct hid_live_state *state,
				     const unsigned char report[HID_REPORT_LEN],
				     int *chars, size_t max_chars)
{
	bool shift = report[0] & (HID_MOD_LSHIFT | HID_MOD_RSHIFT);
	size_t count = 0;
	size_t i;

	for (i = 2; i < HID_REPORT_LEN; i++) {
		unsigned char key = report[i];
		int ch;

		if (key < 4 || hid_key_in_prev(state, key))
			continue;
		ch = hid_key_shell_code(key, shift);
		if (ch && count < max_chars)
			chars[count++] = ch;
	}

	hid_live_copy_prev(state, report);
	return count;
}

static unsigned int le16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static void print_descriptor_hint(const unsigned char *data, size_t len)
{
	if (len < 2)
		return;

	if (data[1] == 1) {
		if (len >= 8) {
			printf("usbhost decode descriptor device-prefix bLength=%u bcdUSB=0x%04x class=0x%02x subclass=0x%02x protocol=0x%02x maxpkt0=%u\n",
			       data[0], le16(&data[2]), data[4], data[5],
			       data[6], data[7]);
		} else {
			printf("usbhost decode descriptor device-prefix truncated=%zu\n",
			       len);
		}
		if (len >= 18) {
			printf("usbhost decode descriptor device-id vid=0x%04x pid=0x%04x bcdDevice=0x%04x configs=%u\n",
			       le16(&data[8]), le16(&data[10]), le16(&data[12]),
			       data[17]);
		}
		return;
	}

	if (data[1] == 2 && len >= 9) {
		printf("usbhost decode descriptor config total=%u interfaces=%u attributes=0x%02x maxpower=%umA\n",
		       le16(&data[2]), data[4], data[7], data[8] * 2u);
		return;
	}

	if (data[1] == 4 && len >= 9) {
		printf("usbhost decode descriptor interface number=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u\n",
		       data[2], data[5], data[6], data[7], data[4]);
		if (data[5] == 3 && data[6] == 1 && data[7] == 1)
			puts("usbhost decode descriptor boot-keyboard-interface yes");
	}
}

static int decode_rx_hex(const char *hex)
{
	unsigned char bytes[RX_BYTES_MAX];
	size_t len = 0;
	size_t pid_index;
	unsigned char pid;

	if (!hex || !*hex) {
		puts("usbhost decode no-rx-data");
		return 1;
	}
	if (parse_hex_bytes(hex, bytes, sizeof(bytes), &len) < 0 || !len) {
		fprintf(stderr, "fruitjam-usbhost: bad RX hex\n");
		return 1;
	}

	pid_index = (len >= 2 && usb_pid_valid(bytes[1])) ? 1 : 0;
	pid = bytes[pid_index];
	if (!usb_pid_valid(pid)) {
		printf("usbhost decode pid-invalid len=%zu byte0=0x%02x",
		       len, bytes[0]);
		if (len >= 2)
			printf(" byte1=0x%02x", bytes[1]);
		putchar('\n');
		return 2;
	}

	printf("usbhost decode packet prefix-bytes=%zu pid=%s raw=0x%02x valid=yes packet-bytes=%zu\n",
	       pid_index, usb_pid_name(pid), pid, len - pid_index);
	if (usb_pid_is_data(pid)) {
		size_t payload_with_crc = len - pid_index - 1;
		size_t data_len = payload_with_crc >= 2 ?
			payload_with_crc - 2 : payload_with_crc;

		printf("usbhost decode data payload-bytes=%zu crc16=%s\n",
		       data_len, payload_with_crc >= 2 ? "present" : "missing");
		if (data_len) {
			print_descriptor_hint(&bytes[pid_index + 1], data_len);
			print_hid_keyboard_hint(&bytes[pid_index + 1], data_len,
						"usbhost decode hid");
		}
	}
	return 0;
}

static int decode_hid_hex(const char *hex)
{
	unsigned char bytes[RX_BYTES_MAX];
	const unsigned char *report = bytes;
	size_t len = 0;
	size_t report_len = 0;
	size_t pid_index;
	unsigned char pid;

	if (!hex || !*hex) {
		puts("usbhost hid no-rx-data");
		return 1;
	}
	if (parse_hex_bytes(hex, bytes, sizeof(bytes), &len) < 0 || !len) {
		fprintf(stderr, "fruitjam-usbhost: bad HID hex\n");
		return 1;
	}

	pid_index = (len >= 2 && usb_pid_valid(bytes[1])) ? 1 : 0;
	pid = bytes[pid_index];
	if (usb_pid_valid(pid) && usb_pid_is_data(pid)) {
		size_t payload_with_crc = len - pid_index - 1;

		if (payload_with_crc < 2) {
			puts("usbhost hid data-packet-missing-crc");
			return 2;
		}
		report = &bytes[pid_index + 1];
		report_len = payload_with_crc - 2;
	} else {
		report = bytes;
		report_len = len;
	}

	if (!print_hid_keyboard_hint(report, report_len, "usbhost hid")) {
		puts("usbhost hid not-boot-keyboard-report");
		return 2;
	}
	return 0;
}

static int hid_report_from_hex(const char *hex,
			       unsigned char report[HID_REPORT_LEN])
{
	unsigned char bytes[RX_BYTES_MAX];
	const unsigned char *src = bytes;
	size_t len = 0;
	size_t report_len = 0;
	size_t pid_index;
	unsigned char pid;

	if (!hex || !*hex)
		return -1;
	if (parse_hex_bytes(hex, bytes, sizeof(bytes), &len) < 0 || !len)
		return -1;

	pid_index = (len >= 2 && usb_pid_valid(bytes[1])) ? 1 : 0;
	pid = bytes[pid_index];
	if (usb_pid_valid(pid) && usb_pid_is_data(pid)) {
		size_t payload_with_crc = len - pid_index - 1;

		if (payload_with_crc < 2)
			return -1;
		src = &bytes[pid_index + 1];
		report_len = payload_with_crc - 2;
	} else {
		src = bytes;
		report_len = len;
	}

	if (report_len != HID_REPORT_LEN || src[1] != 0)
		return -1;
	memcpy(report, src, HID_REPORT_LEN);
	return 0;
}

static int bridge_read_raw(char *buf, size_t len)
{
	int fd;
	ssize_t ret;

	if (!len)
		return -1;
	fd = open(BRIDGE_DEV, O_RDONLY);
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
	buf[ret] = '\0';
	return 0;
}

static int bridge_parse_int(const char *text, const char *key, int *value)
{
	size_t key_len = strlen(key);
	const char *p = text;
	char *end;
	long v;

	while (*p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			errno = 0;
			v = strtol(p + key_len + 1, &end, 0);
			if (errno || end == p + key_len + 1)
				return -1;
			*value = (int)v;
			return 0;
		}

		if (!line_end)
			break;
		p = line_end + 1;
	}

	return -1;
}

static int bridge_parse_string(const char *text, const char *key,
			       char *value, size_t value_len)
{
	size_t key_len = strlen(key);
	const char *p = text;

	if (!value_len)
		return -1;
	while (*p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			size_t n = line_len - key_len - 1;

			if (n >= value_len)
				n = value_len - 1;
			memcpy(value, p + key_len + 1, n);
			value[n] = '\0';
			return 0;
		}

		if (!line_end)
			break;
		p = line_end + 1;
	}

	value[0] = '\0';
	return -1;
}

static int bridge_read_status(struct usbhost_status *st)
{
	char text[BRIDGE_BUF_MAX];

	if (bridge_read_raw(text, sizeof(text)) < 0)
		return -1;
	if (bridge_parse_int(text, "power", &st->power) < 0 ||
	    bridge_parse_int(text, "dp", &st->dp) < 0 ||
	    bridge_parse_int(text, "dm", &st->dm) < 0)
		return -1;

	st->device = usb_state(st->power, st->dp, st->dm);
	st->present = st->power > 0 &&
		(!strcmp(st->device, "full-speed-device") ||
		 !strcmp(st->device, "low-speed-device"));
	if (bridge_parse_int(text, "pio_ready", &st->pio_ready) < 0)
		st->pio_ready = 0;
	if (bridge_parse_int(text, "pio_configured", &st->pio_configured) < 0)
		st->pio_configured = 0;
	if (bridge_parse_int(text, "packets", &st->packets) < 0)
		st->packets = 0;
	if (bridge_parse_int(text, "tx_errors", &st->tx_errors) < 0)
		st->tx_errors = 0;
	if (bridge_parse_int(text, "last_tx_result", &st->last_tx_result) < 0)
		st->last_tx_result = 0;
	if (bridge_parse_int(text, "last_tx_len", &st->last_tx_len) < 0)
		st->last_tx_len = 0;
	if (bridge_parse_int(text, "rx_attempts", &st->rx_attempts) < 0)
		st->rx_attempts = 0;
	if (bridge_parse_int(text, "rx_errors", &st->rx_errors) < 0)
		st->rx_errors = 0;
	if (bridge_parse_int(text, "last_rx_result", &st->last_rx_result) < 0)
		st->last_rx_result = 0;
	if (bridge_parse_int(text, "last_rx_len", &st->last_rx_len) < 0)
		st->last_rx_len = 0;
	if (bridge_parse_int(text, "last_rx_pid", &st->last_rx_pid) < 0)
		st->last_rx_pid = 0;
	if (bridge_parse_string(text, "last_rx_hex", st->last_rx_hex,
				sizeof(st->last_rx_hex)) < 0)
		st->last_rx_hex[0] = '\0';
	st->kernel_bridge = true;
	return 0;
}

static int bridge_write_command(const char *command)
{
	int fd;
	ssize_t ret;
	size_t len = strlen(command);

	fd = open(BRIDGE_DEV, O_WRONLY);
	if (fd < 0)
		return -1;
	ret = write(fd, command, len);
	close(fd);
	return ret == (ssize_t)len ? 0 : -1;
}

static void keyboard_target_default(struct keyboard_target *target)
{
	target->addr = KBD_ADDR_DEFAULT;
	target->config = KBD_CONFIG_DEFAULT;
	target->iface = KBD_IFACE_DEFAULT;
	target->ep = KBD_EP_DEFAULT;
}

static int keyboard_init_command(const struct keyboard_target *target,
				 char *buf, size_t len)
{
	int ret = snprintf(buf, len, "kbd-init %u %u %u",
			   target->addr, target->config, target->iface);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int keyboard_poll_command(const struct keyboard_target *target,
				 char *buf, size_t len)
{
	int ret = snprintf(buf, len, "kbd-poll %u %u",
			   target->addr, target->ep);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static bool keyboard_poll_error_is_transient(int saved_errno,
					     const struct usbhost_status *st)
{
	if (saved_errno == EAGAIN || saved_errno == ETIMEDOUT ||
	    saved_errno == EINTR)
		return true;
	if (st->last_rx_pid == USB_PID_NAK)
		return true;
	if (st->last_rx_result == -EAGAIN || st->last_rx_result == -ETIMEDOUT)
		return true;
	return false;
}

static int keyboard_init_target_common(const struct keyboard_target *target,
				       bool quiet)
{
	struct usbhost_status st;
	char command[48];
	int saved_errno;

	if (keyboard_init_command(target, command, sizeof(command)) < 0) {
		errno = EINVAL;
		return -1;
	}
	if (bridge_write_command(command) < 0) {
		saved_errno = errno;
		if (!quiet) {
			fprintf(stderr, "fruitjam-usbhost: keyboard init failed: %s\n",
				strerror(saved_errno));
			read_status(&st);
			print_human(&st);
		}
		return -1;
	}
	return 0;
}

static int keyboard_init_target(const struct keyboard_target *target)
{
	return keyboard_init_target_common(target, false);
}

static int keyboard_poll_report_common(const struct keyboard_target *target,
				       unsigned char report[HID_REPORT_LEN],
				       bool *has_report,
				       struct usbhost_status *status,
				       bool quiet)
{
	struct usbhost_status st;
	char command[48];
	int saved_errno;

	*has_report = false;
	if (status)
		memset(status, 0, sizeof(*status));
	if (keyboard_poll_command(target, command, sizeof(command)) < 0) {
		errno = EINVAL;
		return -1;
	}
	if (bridge_write_command(command) < 0) {
		saved_errno = errno;
		read_status(&st);
		if (status)
			*status = st;
		if (keyboard_poll_error_is_transient(saved_errno, &st))
			return 0;
		if (!quiet) {
			fprintf(stderr, "fruitjam-usbhost: keyboard poll failed: %s\n",
				strerror(saved_errno));
			print_human(&st);
		}
		return -1;
	}

	read_status(&st);
	if (status)
		*status = st;
	if (hid_report_from_hex(st.last_rx_hex, report) == 0)
		*has_report = true;
	return 0;
}

static int keyboard_poll_report(const struct keyboard_target *target,
				unsigned char report[HID_REPORT_LEN],
				bool *has_report)
{
	return keyboard_poll_report_common(target, report, has_report, NULL,
					   false);
}

static void print_keyboard_target(FILE *out, const char *prefix,
				  const struct keyboard_target *target,
				  const char *source)
{
	fprintf(out, "%s addr=%u config=%u iface=%u ep=%u source=%s\n",
		prefix, target->addr, target->config, target->iface,
		target->ep, source);
}

static int keyboard_find_target(struct keyboard_target *target, FILE *out)
{
	struct keyboard_target idle_target;
	bool have_idle = false;
	unsigned int config;
	unsigned int iface;
	unsigned int ep;

	for (config = 1; config <= KBD_SCAN_CONFIG_MAX; config++) {
		for (iface = 0; iface <= KBD_SCAN_IFACE_MAX; iface++) {
			struct keyboard_target candidate;

			keyboard_target_default(&candidate);
			candidate.config = config;
			candidate.iface = iface;
			if (keyboard_init_target_common(&candidate, true) < 0)
				continue;

			for (ep = 1; ep <= KBD_SCAN_EP_MAX; ep++) {
				struct usbhost_status st;
				unsigned char report[HID_REPORT_LEN];
				bool has_report = false;

				candidate.ep = ep;
				if (keyboard_poll_report_common(&candidate, report,
								&has_report,
								&st, true) < 0)
					continue;
				if (has_report) {
					*target = candidate;
					if (out)
						print_keyboard_target(out,
								      "usbhost keyboard target",
								      target,
								      "report");
					return 0;
				}
				if (!have_idle && st.last_rx_pid == USB_PID_NAK) {
					idle_target = candidate;
					have_idle = true;
				}
			}
		}
	}

	if (have_idle) {
		*target = idle_target;
		if (out)
			print_keyboard_target(out, "usbhost keyboard target",
					      target, "nak");
		return 0;
	}

	fprintf(stderr, "fruitjam-usbhost: no boot-keyboard target found "
		"(scanned configs 1-%u, interfaces 0-%u, endpoints 1-%u)\n",
		KBD_SCAN_CONFIG_MAX, KBD_SCAN_IFACE_MAX, KBD_SCAN_EP_MAX);
	return -1;
}

static int keyboard_poll_once(const struct keyboard_target *target,
			      struct hid_live_state *state, bool events)
{
	unsigned char report[HID_REPORT_LEN];
	bool has_report = false;

	if (keyboard_poll_report(target, report, &has_report) < 0)
		return -1;
	if (has_report)
		process_hid_live_report(state, report, events);
	return 0;
}

static void read_status(struct usbhost_status *st)
{
	if (bridge_read_status(st) == 0)
		return;

	st->power = gpio_read(GPIO_POWER);
	st->dp = gpio_read_input(GPIO_DP);
	st->dm = gpio_read_input(GPIO_DM);
	st->device = usb_state(st->power, st->dp, st->dm);
	st->present = st->power > 0 &&
		(!strcmp(st->device, "full-speed-device") ||
		 !strcmp(st->device, "low-speed-device"));
	st->pio_ready = 0;
	st->pio_configured = 0;
	st->packets = 0;
	st->tx_errors = 0;
	st->last_tx_result = 0;
	st->last_tx_len = 0;
	st->rx_attempts = 0;
	st->rx_errors = 0;
	st->last_rx_result = 0;
	st->last_rx_len = 0;
	st->last_rx_pid = 0;
	st->last_rx_hex[0] = '\0';
	st->kernel_bridge = false;
}

static void print_status_line(const struct usbhost_status *st)
{
	printf("usbhost power %s gpio%u=%d", st->power ? "on" : "off",
	       GPIO_POWER, st->power);
	if (st->dp >= 0 && st->dm >= 0)
		printf(" gpio%u(dp)=%d gpio%u(dm)=%d",
		       GPIO_DP, st->dp, GPIO_DM, st->dm);
	putchar('\n');
}

static void print_human(const struct usbhost_status *st)
{
	print_status_line(st);
	printf("usbhost device %s\n", st->device);
	printf("usbhost hid-ready %s\n", st->present ? "line-detected" : "no");
	printf("usbhost stack %s\n",
	       st->kernel_bridge ?
	       (st->pio_ready > 0 ? kernel_pio_stack_text : kernel_stack_text) :
	       sysfs_stack_text);
	if (st->kernel_bridge) {
		printf("usbhost pio-ready %s\n", st->pio_ready > 0 ? "yes" : "no");
		printf("usbhost pio-configured %s\n",
		       st->pio_configured > 0 ? "yes" : "no");
		printf("usbhost packets %d tx-errors %d last-tx-result %d len %d\n",
		       st->packets, st->tx_errors, st->last_tx_result,
		       st->last_tx_len);
		printf("usbhost rx-attempts %d rx-errors %d last-rx-result %d pid 0x%02x len %d\n",
		       st->rx_attempts, st->rx_errors, st->last_rx_result,
		       st->last_rx_pid, st->last_rx_len);
		if (st->last_rx_hex[0])
			printf("usbhost last-rx-hex %s\n", st->last_rx_hex);
	}
	puts("usbhost next pio-packet-io first=boot-protocol-keyboard");
}

static void print_json(const struct usbhost_status *st)
{
	printf("{\"ok\":%s,\"power\":%d,\"dp\":%d,\"dm\":%d,\"device\":\"%s\",",
	       st->power >= 0 ? "true" : "false",
	       st->power, st->dp, st->dm, st->device);
	printf("\"present\":%s,\"hid\":false,\"driver\":\"%s\",",
	       st->present ? "true" : "false",
	       st->kernel_bridge ? "kernel-line-state" : "sysfs-line-state");
	printf("\"stack\":\"%s\",",
	       st->kernel_bridge ?
	       (st->pio_ready > 0 ? kernel_pio_stack_text : kernel_stack_text) :
	       sysfs_stack_text);
	printf("\"pio_ready\":%s,", st->pio_ready > 0 ? "true" : "false");
	printf("\"pio_configured\":%s,", st->pio_configured > 0 ? "true" : "false");
	printf("\"packets\":%d,\"tx_errors\":%d,", st->packets, st->tx_errors);
	printf("\"last_tx_result\":%d,\"last_tx_len\":%d,",
	       st->last_tx_result, st->last_tx_len);
	printf("\"rx_attempts\":%d,\"rx_errors\":%d,",
	       st->rx_attempts, st->rx_errors);
	printf("\"last_rx_result\":%d,\"last_rx_len\":%d,",
	       st->last_rx_result, st->last_rx_len);
	printf("\"last_rx_pid\":%d,\"last_rx_hex\":\"%s\",",
	       st->last_rx_pid, st->last_rx_hex);
	printf("\"next\":\"pio-packet-io\",");
	printf("\"first_milestone\":\"boot-protocol-keyboard\"}\n");
}

static int bridge_action(const char *command, const char *label)
{
	struct usbhost_status st;
	int saved_errno;

	if (access(BRIDGE_DEV, W_OK) != 0) {
		fprintf(stderr, "fruitjam-usbhost: %s requires %s\n",
			label, BRIDGE_DEV);
		return 1;
	}
	if (bridge_write_command(command) < 0) {
		saved_errno = errno;
		fprintf(stderr, "fruitjam-usbhost: bridge %s failed: %s\n",
			label, strerror(saved_errno));
		read_status(&st);
		print_human(&st);
		return 1;
	}
	read_status(&st);
	print_human(&st);
	if (st.last_rx_hex[0])
		decode_rx_hex(st.last_rx_hex);
	return st.power >= 0 ? 0 : 1;
}

static int parse_seconds(const char *s, unsigned int default_value,
			 unsigned int *seconds)
{
	char *end;
	unsigned long value;

	if (!s || !*s) {
		*seconds = default_value;
		return 0;
	}
	errno = 0;
	value = strtoul(s, &end, 10);
	if (errno || *end || value > MAX_SECONDS)
		return -1;
	*seconds = (unsigned int)value;
	return 0;
}

static int parse_reset_ms(const char *s, unsigned int *ms)
{
	char *end;
	unsigned long value;

	if (!s || !*s) {
		*ms = DEFAULT_RESET_MS;
		return 0;
	}
	errno = 0;
	value = strtoul(s, &end, 10);
	if (errno || *end || value < 10 || value > MAX_RESET_MS)
		return -1;
	*ms = (unsigned int)value;
	return 0;
}

static int parse_uint_range(const char *s, unsigned int min,
			    unsigned int max, unsigned int *value)
{
	char *end;
	unsigned long parsed;

	if (!s || !*s)
		return -1;
	errno = 0;
	parsed = strtoul(s, &end, 10);
	if (errno || *end || parsed < min || parsed > max)
		return -1;
	*value = (unsigned int)parsed;
	return 0;
}

static int parse_keyboard_init_args(int argc, char **argv, int start,
				    struct keyboard_target *target)
{
	keyboard_target_default(target);
	if (argc == start)
		return 0;
	if (argc - start != 3)
		return -1;
	return parse_uint_range(argv[start], 1, 127, &target->addr) ||
		parse_uint_range(argv[start + 1], 1, 255, &target->config) ||
		parse_uint_range(argv[start + 2], 0, 255, &target->iface);
}

static int parse_keyboard_poll_args(int argc, char **argv, int start,
				    struct keyboard_target *target)
{
	keyboard_target_default(target);
	if (argc == start)
		return 0;
	if (argc - start != 2)
		return -1;
	return parse_uint_range(argv[start], 1, 127, &target->addr) ||
		parse_uint_range(argv[start + 1], 1, 15, &target->ep);
}

static int parse_keyboard_full_args(int argc, char **argv, int start,
				    struct keyboard_target *target)
{
	keyboard_target_default(target);
	if (argc == start)
		return 0;
	if (argc - start != 4)
		return -1;
	return parse_uint_range(argv[start], 1, 127, &target->addr) ||
		parse_uint_range(argv[start + 1], 1, 255, &target->config) ||
		parse_uint_range(argv[start + 2], 0, 255, &target->iface) ||
		parse_uint_range(argv[start + 3], 1, 15, &target->ep);
}

static int bus_reset(unsigned int reset_ms)
{
	int ret = 0;

	if (gpio_write(GPIO_POWER, 1) < 0)
		ret = -1;
	usleep(POST_RESET_US);
	if (gpio_write(GPIO_DP, 0) < 0)
		ret = -1;
	if (gpio_write(GPIO_DM, 0) < 0)
		ret = -1;
	usleep(reset_ms * 1000u);
	if (gpio_input(GPIO_DP) < 0)
		ret = -1;
	if (gpio_input(GPIO_DM) < 0)
		ret = -1;
	usleep(POST_RESET_US);
	return ret;
}

static int wait_for_device(unsigned int seconds)
{
	struct usbhost_status st;
	unsigned int loops = seconds * 10u;
	unsigned int i;

	for (i = 0; i <= loops; i++) {
		read_status(&st);
		if (st.present) {
			print_human(&st);
			return 0;
		}
		if (i < loops)
			usleep(POLL_US);
	}
	print_human(&st);
	return 2;
}

static int monitor_device(unsigned int seconds)
{
	struct usbhost_status st;
	char prev[32] = "";
	unsigned int loops = seconds * 10u;
	unsigned int i;

	for (i = 0; i <= loops; i++) {
		read_status(&st);
		if (strcmp(prev, st.device)) {
			printf("t=%u00ms ", i);
			print_status_line(&st);
			printf("usbhost device %s\n", st.device);
			snprintf(prev, sizeof(prev), "%s", st.device);
		}
		if (i < loops)
			usleep(POLL_US);
	}
	return 0;
}

static char *trim_text(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == '\n' || end[-1] == '\r' ||
			   end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	return s;
}

static int split_args(char *line, char **argv, int max_args)
{
	int argc = 0;

	while (*line && argc < max_args - 1) {
		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line)
			break;
		argv[argc++] = line;
		while (*line && *line != ' ' && *line != '\t')
			line++;
		if (*line)
			*line++ = '\0';
	}
	argv[argc] = NULL;
	return argc;
}

static void exec_child(char **argv)
{
	char path[96];
	unsigned int i;

	if (strchr(argv[0], '/')) {
		execv(argv[0], argv);
		perror(argv[0]);
		_exit(127);
	}

	for (i = 0; i < ARRAY_SIZE(path_dirs); i++) {
		snprintf(path, sizeof(path), "%s/%s", path_dirs[i], argv[0]);
		execv(path, argv);
		if (errno != ENOENT && errno != ENOTDIR) {
			perror(path);
			_exit(127);
		}
	}

	fprintf(stderr, "%s: not found\n", argv[0]);
	_exit(127);
}

static int run_command(char **argv)
{
	pid_t pid = vfork();
	int status;

	if (pid < 0) {
		perror("vfork");
		return 1;
	}
	if (pid == 0)
		exec_child(argv);

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return 1;
	}
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}

static void keyboard_shell_prompt(void)
{
	fputs("usbkbd$ ", stdout);
	fflush(stdout);
}

static int keyboard_shell_append_text(char *line, size_t *len,
				      size_t line_size, const char *text)
{
	while (*text && *len + 1 < line_size) {
		line[(*len)++] = *text++;
		putchar(line[*len - 1]);
	}
	line[*len] = '\0';
	fflush(stdout);
	return *text == '\0';
}

static void keyboard_shell_redraw_line(const char *line)
{
	fputs("\r\033[K", stdout);
	keyboard_shell_prompt();
	fputs(line, stdout);
	fflush(stdout);
}

static void update_common_prefix(char *common, const char *name)
{
	size_t i = 0;

	while (common[i] && name[i] && common[i] == name[i])
		i++;
	common[i] = '\0';
}

static void note_completion_match(const char *name, const char *prefix,
				  char *common, size_t common_size,
				  int *matches)
{
	size_t prefix_len = strlen(prefix);

	if (strncmp(name, prefix, prefix_len))
		return;
	if (*matches > 0 && !strcmp(common, name))
		return;
	if (*matches == 0) {
		snprintf(common, common_size, "%s", name);
	} else {
		update_common_prefix(common, name);
	}
	(*matches)++;
}

static int path_is_dir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void note_path_match(const char *dir_prefix, const char *name,
			    const char *token, char *common,
			    size_t common_size, int *matches)
{
	char candidate[KBD_SHELL_LINE_SIZE];
	size_t len;
	int ret;

	ret = snprintf(candidate, sizeof(candidate), "%s%s", dir_prefix, name);
	if (ret <= 0 || (size_t)ret >= sizeof(candidate))
		return;
	len = (size_t)ret;
	if (path_is_dir(candidate) && len + 1 < sizeof(candidate)) {
		candidate[len++] = '/';
		candidate[len] = '\0';
	}
	note_completion_match(candidate, token, common, common_size, matches);
}

static int keyboard_complete_command(char *line, size_t *len,
				     size_t line_size)
{
	char prefix[64];
	char common[64];
	size_t i;
	int matches = 0;

	for (i = 0; i < *len; i++) {
		if (line[i] == ' ' || line[i] == '\t') {
			putchar('\a');
			fflush(stdout);
			return 0;
		}
	}
	if (*len >= sizeof(prefix)) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	memcpy(prefix, line, *len);
	prefix[*len] = '\0';
	common[0] = '\0';

	for (i = 0; i < ARRAY_SIZE(keyboard_builtins); i++)
		note_completion_match(keyboard_builtins[i], prefix, common,
				      sizeof(common), &matches);
	for (i = 0; i < ARRAY_SIZE(path_dirs); i++) {
		DIR *dir = opendir(path_dirs[i]);
		struct dirent *de;

		if (!dir)
			continue;
		while ((de = readdir(dir)))
			note_completion_match(de->d_name, prefix, common,
					      sizeof(common), &matches);
		closedir(dir);
	}

	if (matches == 0) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	if (strlen(common) > *len)
		keyboard_shell_append_text(line, len, line_size,
					   common + *len);
	if (matches == 1 && *len + 1 < line_size)
		keyboard_shell_append_text(line, len, line_size, " ");
	else if (matches > 1 && strlen(common) == *len)
		putchar('\a');
	fflush(stdout);
	return 1;
}

static int keyboard_complete_path(char *line, size_t *len, size_t line_size,
				  size_t token_start)
{
	char token[KBD_SHELL_LINE_SIZE];
	char dir[KBD_SHELL_LINE_SIZE];
	char dir_prefix[KBD_SHELL_LINE_SIZE];
	char common[KBD_SHELL_LINE_SIZE];
	const char *name_prefix;
	char *slash;
	DIR *dh;
	struct dirent *de;
	size_t token_len = *len - token_start;
	int matches = 0;

	if (token_len >= sizeof(token)) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	memcpy(token, line + token_start, token_len);
	token[token_len] = '\0';

	slash = strrchr(token, '/');
	if (slash) {
		size_t prefix_len = (size_t)(slash - token) + 1;

		if (prefix_len >= sizeof(dir_prefix)) {
			putchar('\a');
			fflush(stdout);
			return 0;
		}
		memcpy(dir_prefix, token, prefix_len);
		dir_prefix[prefix_len] = '\0';
		if (prefix_len == 1 && token[0] == '/') {
			snprintf(dir, sizeof(dir), "/");
		} else {
			memcpy(dir, token, prefix_len - 1);
			dir[prefix_len - 1] = '\0';
		}
		name_prefix = slash + 1;
	} else {
		snprintf(dir, sizeof(dir), ".");
		dir_prefix[0] = '\0';
		name_prefix = token;
	}

	dh = opendir(dir);
	if (!dh) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	common[0] = '\0';
	while ((de = readdir(dh))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (strncmp(de->d_name, name_prefix, strlen(name_prefix)))
			continue;
		note_path_match(dir_prefix, de->d_name, token, common,
				sizeof(common), &matches);
	}
	closedir(dh);

	if (matches == 0) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	if (strlen(common) > token_len)
		keyboard_shell_append_text(line, len, line_size,
					   common + token_len);
	if (matches == 1 && *len > 0 && line[*len - 1] != '/' &&
	    *len + 1 < line_size)
		keyboard_shell_append_text(line, len, line_size, " ");
	else if (matches > 1 && strlen(common) == token_len)
		putchar('\a');
	fflush(stdout);
	return 1;
}

static int keyboard_complete_line(char *line, size_t *len, size_t line_size)
{
	size_t token_start = *len;

	while (token_start > 0 &&
	       line[token_start - 1] != ' ' &&
	       line[token_start - 1] != '\t')
		token_start--;
	if (token_start == 0)
		return keyboard_complete_command(line, len, line_size);
	return keyboard_complete_path(line, len, line_size, token_start);
}

static void keyboard_add_history(
	char history[KBD_HISTORY_DEPTH][KBD_SHELL_LINE_SIZE],
	int *history_count, const char *cmd)
{
	if (!*cmd)
		return;
	if (*history_count > 0 &&
	    !strcmp(history[*history_count - 1], cmd))
		return;
	if (*history_count == KBD_HISTORY_DEPTH) {
		memmove(history[0], history[1],
			(KBD_HISTORY_DEPTH - 1) * KBD_SHELL_LINE_SIZE);
		(*history_count)--;
	}
	snprintf(history[*history_count], KBD_SHELL_LINE_SIZE, "%s", cmd);
	(*history_count)++;
}

static int keyboard_shell_run_line(
	char *line, int last_status,
	char history[KBD_HISTORY_DEPTH][KBD_SHELL_LINE_SIZE],
	int history_count)
{
	char *cmd = trim_text(line);
	char *argv[16];
	int argc;

	if (!*cmd)
		return last_status;
	argc = split_args(cmd, argv, ARRAY_SIZE(argv));
	if (argc == 0)
		return last_status;
	if (!strcmp(argv[0], "exit"))
		return -1;
	if (!strcmp(argv[0], "echo")) {
		int i;

		for (i = 1; i < argc; i++) {
			if (i > 1)
				putchar(' ');
			fputs(argv[i], stdout);
		}
		putchar('\n');
		return 0;
	}
	if (!strcmp(argv[0], "cd")) {
		const char *dir = argc > 1 ? argv[1] : "/";

		if (chdir(dir) < 0) {
			perror(dir);
			return 1;
		}
		return 0;
	}
	if (!strcmp(argv[0], "?") || !strcmp(argv[0], "help")) {
		puts("builtins: cd echo exit help history status");
		puts("simple commands are searched in /bin /usr/bin /sbin /usr/sbin");
		puts("line editing: up/down history, tab command/path completion");
		return 0;
	}
	if (!strcmp(argv[0], "history")) {
		int i;

		for (i = 0; i < history_count; i++)
			printf("%d %s\n", i + 1, history[i]);
		return 0;
	}
	if (!strcmp(argv[0], "status")) {
		printf("%d\n", last_status);
		return last_status;
	}
	return run_command(argv);
}

static void keyboard_shell_accept_char(char *line, size_t *len,
				       char history[KBD_HISTORY_DEPTH][KBD_SHELL_LINE_SIZE],
				       int *history_count, int *history_pos,
				       int ch, int *last_status, bool *done)
{
	if (ch == KBD_CH_UP) {
		if (*history_count > 0) {
			if (*history_pos > 0)
				(*history_pos)--;
			snprintf(line, KBD_SHELL_LINE_SIZE, "%s",
				 history[*history_pos]);
			*len = strlen(line);
			keyboard_shell_redraw_line(line);
		} else {
			putchar('\a');
			fflush(stdout);
		}
		return;
	}
	if (ch == KBD_CH_DOWN) {
		if (*history_count > 0) {
			if (*history_pos < *history_count - 1) {
				(*history_pos)++;
				snprintf(line, KBD_SHELL_LINE_SIZE, "%s",
					 history[*history_pos]);
			} else {
				*history_pos = *history_count;
				line[0] = '\0';
			}
			*len = strlen(line);
			keyboard_shell_redraw_line(line);
		} else {
			putchar('\a');
			fflush(stdout);
		}
		return;
	}
	if (ch == KBD_CH_LEFT || ch == KBD_CH_RIGHT) {
		putchar('\a');
		fflush(stdout);
		return;
	}
	if (ch == '\n') {
		char saved[KBD_SHELL_LINE_SIZE];
		char *cmd;

		putchar('\n');
		line[*len] = '\0';
		snprintf(saved, sizeof(saved), "%s", line);
		cmd = trim_text(saved);
		keyboard_add_history(history, history_count, cmd);
		*history_pos = *history_count;
		*last_status = keyboard_shell_run_line(line, *last_status,
						       history,
						       *history_count);
		if (*last_status < 0) {
			*done = true;
			return;
		}
		*len = 0;
		line[0] = '\0';
		keyboard_shell_prompt();
		return;
	}
	if (ch == '\177') {
		if (*len > 0) {
			(*len)--;
			line[*len] = '\0';
			fputs("\b \b", stdout);
			fflush(stdout);
		}
		*history_pos = *history_count;
		return;
	}
	if (ch == '\t') {
		keyboard_complete_line(line, len, KBD_SHELL_LINE_SIZE);
		*history_pos = *history_count;
		return;
	}
	if (ch >= 32 && ch <= 126 && *len + 1 < KBD_SHELL_LINE_SIZE) {
		line[(*len)++] = (char)ch;
		line[*len] = '\0';
		putchar(ch);
		fflush(stdout);
		*history_pos = *history_count;
	}
}

static int keyboard_shell(const struct keyboard_target *target,
			  unsigned int seconds)
{
	struct hid_live_state state;
	char history[KBD_HISTORY_DEPTH][KBD_SHELL_LINE_SIZE];
	char line[KBD_SHELL_LINE_SIZE];
	size_t line_len = 0;
	int history_count = 0;
	int history_pos = 0;
	unsigned int loops;
	unsigned int i;
	int last_status = 0;
	bool done = false;

	if (access(BRIDGE_DEV, W_OK) != 0) {
		fprintf(stderr, "fruitjam-usbhost: keyboard shell requires %s\n",
			BRIDGE_DEV);
		return 1;
	}
	if (keyboard_init_target(target) < 0)
		return 1;

	memset(&state, 0, sizeof(state));
	line[0] = '\0';
	loops = seconds ? (seconds * 1000000u) / KBD_POLL_US : 0;
	puts("USB keyboard shell; type exit to leave");
	keyboard_shell_prompt();

	for (i = 0; !done && (!seconds || i < loops); i++) {
		unsigned char report[HID_REPORT_LEN];
		bool has_report = false;

		if (keyboard_poll_report(target, report, &has_report) < 0)
			return 1;
		if (has_report) {
			int chars[HID_KEY_SLOTS];
			size_t count = collect_hid_text_chars(&state, report,
							      chars,
							      ARRAY_SIZE(chars));
			size_t j;

			for (j = 0; j < count && !done; j++)
				keyboard_shell_accept_char(line, &line_len,
							   history,
							   &history_count,
							   &history_pos,
							   chars[j],
							   &last_status,
							   &done);
		}
		if (!done)
			usleep(KBD_POLL_US);
	}
	if (!done)
		putchar('\n');
	return last_status < 0 ? 0 : last_status;
}

static int keyboard_live(const struct keyboard_target *target,
			 unsigned int seconds, bool events)
{
	struct hid_live_state state;
	unsigned int loops;
	unsigned int i;

	if (access(BRIDGE_DEV, W_OK) != 0) {
		fprintf(stderr, "fruitjam-usbhost: keyboard polling requires %s\n",
			BRIDGE_DEV);
		return 1;
	}

	if (keyboard_init_target(target) < 0)
		return 1;

	memset(&state, 0, sizeof(state));
	loops = seconds ? (seconds * 1000000u) / KBD_POLL_US : 1;
	if (!loops)
		loops = 1;

	if (seconds)
		fprintf(stderr, "usbhost keyboard %s for %us\n",
			events ? "events" : "text", seconds);
	else
		fprintf(stderr, "usbhost keyboard %s single poll\n",
			events ? "events" : "text");

	for (i = 0; i < loops; i++) {
		if (keyboard_poll_once(target, &state, events) < 0)
			return 1;
		if (i + 1 < loops)
			usleep(KBD_POLL_US);
	}

	if (!events)
		putchar('\n');
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "status";
	struct usbhost_status st;
	struct keyboard_target target;
	unsigned int seconds;
	unsigned int reset_ms;
	char command[64];

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "on")) {
		if (access(BRIDGE_DEV, W_OK) == 0) {
			if (bridge_write_command("on") < 0) {
				fprintf(stderr, "fruitjam-usbhost: bridge power on failed: %s\n",
					strerror(errno));
				return 1;
			}
		} else if (gpio_write(GPIO_POWER, 1) < 0) {
			fprintf(stderr, "fruitjam-usbhost: power on failed: %s\n",
				strerror(errno));
			return 1;
		}
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "off")) {
		if (access(BRIDGE_DEV, W_OK) == 0) {
			if (bridge_write_command("off") < 0) {
				fprintf(stderr, "fruitjam-usbhost: bridge power off failed: %s\n",
					strerror(errno));
				return 1;
			}
		} else if (gpio_write(GPIO_POWER, 0) < 0) {
			fprintf(stderr, "fruitjam-usbhost: power off failed: %s\n",
				strerror(errno));
			return 1;
		}
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "reset")) {
		if (parse_reset_ms(argc > 2 ? argv[2] : NULL, &reset_ms) < 0) {
			usage(stderr);
			return 1;
		}
		if (access(BRIDGE_DEV, W_OK) == 0) {
			char bridge_cmd[32];

			snprintf(bridge_cmd, sizeof(bridge_cmd), "reset %u", reset_ms);
			if (bridge_write_command(bridge_cmd) < 0) {
				fprintf(stderr, "fruitjam-usbhost: bridge USB bus reset failed: %s\n",
					strerror(errno));
				return 1;
			}
		} else if (bus_reset(reset_ms) < 0) {
			fprintf(stderr, "fruitjam-usbhost: USB bus reset failed: %s\n",
				strerror(errno));
			return 1;
		}
		printf("usbhost reset %ums\n", reset_ms);
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "pio-init"))
		return bridge_action("pio-init", "PIO init");
	if (!strcmp(cmd, "tx-test"))
		return bridge_action("tx-test", "PIO TX test");
	if (!strcmp(cmd, "self-rx"))
		return bridge_action("self-rx", "PIO self-RX probe");
	if (!strcmp(cmd, "sof-burst"))
		return bridge_action("sof-burst", "PIO SOF burst");
	if (!strcmp(cmd, "in-token"))
		return bridge_action("in-token", "PIO IN token probe");
	if (!strcmp(cmd, "setup-token-self-rx"))
		return bridge_action("setup-token-self-rx",
				     "PIO SETUP token self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx"))
		return bridge_action("setup-data-self-rx",
				     "PIO SETUP DATA0 self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx-noeop"))
		return bridge_action("setup-data-self-rx-noeop",
				     "PIO SETUP DATA0 no-EOP-stop self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx-cpu"))
		return bridge_action("setup-data-self-rx-cpu",
				     "PIO CPU-TX SETUP DATA0 self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx-drain"))
		return bridge_action("setup-data-self-rx-drain",
				     "PIO DMA-TX live-drain SETUP DATA0 self-RX probe");
	if (!strcmp(cmd, "data-len-sweep"))
		return bridge_action("data-len-sweep",
				     "PIO DATA0 payload length self-RX sweep");
	if (!strcmp(cmd, "get-device-8"))
		return bridge_action("get-device-8", "PIO GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "in-token-gated"))
		return bridge_action("in-token-gated", "PIO gated IN token probe");
	if (!strcmp(cmd, "get-device-8-gated"))
		return bridge_action("get-device-8-gated",
				     "PIO gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-gated-cpu"))
		return bridge_action("get-device-8-gated-cpu",
				     "PIO CPU-TX gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-combo"))
		return bridge_action("get-device-8-combo",
				     "PIO combined SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-combo-skipack"))
		return bridge_action("get-device-8-combo-skipack",
				     "PIO combined SETUP skip-ACK GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-fast"))
		return bridge_action("get-device-8-fast",
				     "PIO fast gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-tight"))
		return bridge_action("get-device-8-tight",
				     "PIO tight SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-burst"))
		return bridge_action("get-device-8-burst",
				     "PIO burst SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-stream"))
		return bridge_action("get-device-8-stream",
				     "PIO streamed SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8"))
		return bridge_action("reset-get-device-8",
				     "PIO reset/SOF GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-gated"))
		return bridge_action("reset-get-device-8-gated",
				     "PIO reset/SOF gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-combo"))
		return bridge_action("reset-get-device-8-combo",
				     "PIO reset/SOF combined SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-combo-skipack"))
		return bridge_action("reset-get-device-8-combo-skipack",
				     "PIO reset/SOF combined SETUP skip-ACK GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-fast"))
		return bridge_action("reset-get-device-8-fast",
				     "PIO reset/SOF fast gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-tight"))
		return bridge_action("reset-get-device-8-tight",
				     "PIO reset/SOF tight SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-burst"))
		return bridge_action("reset-get-device-8-burst",
				     "PIO reset/SOF burst SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-stream"))
		return bridge_action("reset-get-device-8-stream",
				     "PIO reset/SOF streamed SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "kbd-init")) {
		if (parse_keyboard_init_args(argc, argv, 2, &target) < 0 ||
		    keyboard_init_command(&target, command, sizeof(command)) < 0) {
			usage(stderr);
			return 1;
		}
		return bridge_action(command,
				     "PIO boot-keyboard address/config/protocol probe");
	}
	if (!strcmp(cmd, "kbd-poll")) {
		if (parse_keyboard_poll_args(argc, argv, 2, &target) < 0 ||
		    keyboard_poll_command(&target, command, sizeof(command)) < 0) {
			usage(stderr);
			return 1;
		}
		return bridge_action(command,
				     "PIO boot-keyboard interrupt IN poll");
	}
	if (!strcmp(cmd, "kbd-init-poll")) {
		if (parse_keyboard_full_args(argc, argv, 2, &target) < 0) {
			usage(stderr);
			return 1;
		}
		if (keyboard_init_target(&target) < 0)
			return 1;
		if (keyboard_poll_command(&target, command, sizeof(command)) < 0) {
			usage(stderr);
			return 1;
		}
		return bridge_action(command,
				     "PIO boot-keyboard init and interrupt IN poll");
	}
	if (!strcmp(cmd, "kbd-find")) {
		return keyboard_find_target(&target, stdout) < 0 ? 1 : 0;
	}
	if (!strcmp(cmd, "kbd-text")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL,
				  DEFAULT_KBD_SECONDS, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		if (parse_keyboard_full_args(argc, argv, argc > 2 ? 3 : 2,
					     &target) < 0) {
			usage(stderr);
			return 1;
		}
		return keyboard_live(&target, seconds, false);
	}
	if (!strcmp(cmd, "kbd-events") || !strcmp(cmd, "kbd-monitor")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL,
				  DEFAULT_KBD_SECONDS, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		if (parse_keyboard_full_args(argc, argv, argc > 2 ? 3 : 2,
					     &target) < 0) {
			usage(stderr);
			return 1;
		}
		return keyboard_live(&target, seconds, true);
	}
	if (!strcmp(cmd, "kbd-shell")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL, 0, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		if (parse_keyboard_full_args(argc, argv, argc > 2 ? 3 : 2,
					     &target) < 0) {
			usage(stderr);
			return 1;
		}
		return keyboard_shell(&target, seconds);
	}
	if (!strcmp(cmd, "kbd-auto-text")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL,
				  DEFAULT_KBD_SECONDS, &seconds) < 0 ||
		    argc > 3) {
			usage(stderr);
			return 1;
		}
		if (keyboard_find_target(&target, stderr) < 0)
			return 1;
		return keyboard_live(&target, seconds, false);
	}
	if (!strcmp(cmd, "kbd-auto-events") ||
	    !strcmp(cmd, "kbd-auto-monitor")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL,
				  DEFAULT_KBD_SECONDS, &seconds) < 0 ||
		    argc > 3) {
			usage(stderr);
			return 1;
		}
		if (keyboard_find_target(&target, stderr) < 0)
			return 1;
		return keyboard_live(&target, seconds, true);
	}
	if (!strcmp(cmd, "kbd-auto-shell")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL, 0, &seconds) < 0 ||
		    argc > 3) {
			usage(stderr);
			return 1;
		}
		if (keyboard_find_target(&target, stderr) < 0)
			return 1;
		return keyboard_shell(&target, seconds);
	}
	if (!strcmp(cmd, "status")) {
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "json")) {
		read_status(&st);
		print_json(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "decode")) {
		if (argc > 2)
			return decode_rx_hex(argv[2]);
		read_status(&st);
		return decode_rx_hex(st.last_rx_hex);
	}
	if (!strcmp(cmd, "hid")) {
		if (argc > 2)
			return decode_hid_hex(argv[2]);
		read_status(&st);
		return decode_hid_hex(st.last_rx_hex);
	}
	if (!strcmp(cmd, "wait")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL, 5, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		return wait_for_device(seconds);
	}
	if (!strcmp(cmd, "monitor")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL, 10, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		return monitor_device(seconds);
	}

	usage(stderr);
	return 1;
}
