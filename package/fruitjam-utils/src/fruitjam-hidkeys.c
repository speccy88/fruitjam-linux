// SPDX-License-Identifier: MIT
/*
 * Tiny USB HID boot-keyboard report decoder for Fruit Jam bring-up.
 *
 * The GPIO1/GPIO2 PIO USB host bridge reports packet bytes while the protocol
 * driver is still coming up. Keeping the key mapping here makes both raw
 * 8-byte reports and captured DATA packets testable before polling lands.
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_LEN 8
#define KEY_SLOTS 6
#define INPUT_BYTES_MAX 128
#define USB_PACKET_CRC_LEN 2
#define MOD_LSHIFT 0x02u
#define MOD_RSHIFT 0x20u

struct hidkeys_state {
	unsigned char prev[KEY_SLOTS];
};

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-hidkeys [--events] [REPORT-HEX ...]\n"
		"\n"
		"Decode USB HID boot-keyboard 8-byte reports. Reports may be\n"
		"written as 16 hex digits or separated with spaces, ':' or ','.\n"
		"Input may also be a DATA0/DATA1 last_rx_hex packet from\n"
		"fruitjam-usbhost; the optional bridge prefix and trailing USB\n"
		"CRC16 are stripped when the payload is a boot-keyboard report.\n");
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool is_sep(int c)
{
	return isspace((unsigned char)c) || c == ':' || c == ',' || c == '-' ||
		c == '_';
}

static bool usb_pid_valid(unsigned char pid)
{
	return (((pid >> 4) ^ (pid & 0x0f)) == 0x0f);
}

static bool usb_pid_is_data(unsigned char pid)
{
	return pid == 0xc3 || pid == 0x4b || pid == 0x87 || pid == 0x0f;
}

static const char *key_name(unsigned char key)
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

static int key_ascii(unsigned char key, bool shift)
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

static bool key_in_prev(const struct hidkeys_state *state, unsigned char key)
{
	size_t i;

	for (i = 0; i < KEY_SLOTS; i++) {
		if (state->prev[i] == key)
			return true;
	}
	return false;
}

static bool key_in_report(const unsigned char report[REPORT_LEN], unsigned char key)
{
	size_t i;

	for (i = 2; i < REPORT_LEN; i++) {
		if (report[i] == key)
			return true;
	}
	return false;
}

static void copy_prev(struct hidkeys_state *state,
		      const unsigned char report[REPORT_LEN])
{
	size_t i;

	for (i = 0; i < KEY_SLOTS; i++)
		state->prev[i] = report[i + 2];
}

static void print_char_event(const char *kind, unsigned char key, int ch,
			     unsigned char mod)
{
	if (ch >= 32 && ch <= 126)
		printf("%s key=%s char=%c code=0x%02x modifiers=0x%02x\n",
		       kind, key_name(key), ch, key, mod);
	else if (ch == '\n')
		printf("%s key=%s char=enter code=0x%02x modifiers=0x%02x\n",
		       kind, key_name(key), key, mod);
	else if (ch == '\t')
		printf("%s key=%s char=tab code=0x%02x modifiers=0x%02x\n",
		       kind, key_name(key), key, mod);
	else if (ch == '\177')
		printf("%s key=%s char=backspace code=0x%02x modifiers=0x%02x\n",
		       kind, key_name(key), key, mod);
	else if (ch == '\033')
		printf("%s key=%s char=esc code=0x%02x modifiers=0x%02x\n",
		       kind, key_name(key), key, mod);
	else
		printf("%s key=%s code=0x%02x modifiers=0x%02x\n",
		       kind, key_name(key), key, mod);
}

static void process_report(struct hidkeys_state *state,
			   const unsigned char report[REPORT_LEN],
			   bool events)
{
	bool shift = report[0] & (MOD_LSHIFT | MOD_RSHIFT);
	size_t i;

	for (i = 0; i < KEY_SLOTS; i++) {
		unsigned char key = state->prev[i];

		if (key >= 4 && !key_in_report(report, key) && events)
			printf("release key=%s code=0x%02x\n", key_name(key), key);
	}

	for (i = 2; i < REPORT_LEN; i++) {
		unsigned char key = report[i];
		int ch;

		if (key < 4 || key_in_prev(state, key))
			continue;

		ch = key_ascii(key, shift);
		if (events) {
			print_char_event("press", key, ch, report[0]);
		} else if (ch) {
			putchar(ch);
		}
	}

	copy_prev(state, report);
	if (!events)
		fflush(stdout);
}

static int feed_byte(unsigned char byte, unsigned char report[REPORT_LEN],
		     size_t *report_pos, struct hidkeys_state *state,
		     bool events)
{
	report[*report_pos] = byte;
	(*report_pos)++;
	if (*report_pos == REPORT_LEN) {
		process_report(state, report, events);
		*report_pos = 0;
	}
	return 0;
}

static int parse_hex_bytes(const char *text, unsigned char *bytes,
			   size_t max_bytes, size_t *byte_count)
{
	const unsigned char *p = (const unsigned char *)text;
	size_t n = 0;
	int hi = -1;

	while (*p) {
		int v;

		if (*p == '#')
			break;
		if (is_sep(*p)) {
			p++;
			continue;
		}
		if (*p == 'x' || *p == 'X') {
			p++;
			continue;
		}

		v = hexval(*p++);
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
	}

	if (hi >= 0)
		return -1;
	*byte_count = n;
	return 0;
}

static int usb_data_payload(const unsigned char *bytes, size_t len,
			    const unsigned char **payload, size_t *payload_len)
{
	size_t pid_index;
	size_t payload_with_crc;
	unsigned char pid;

	if (!len)
		return 0;

	pid_index = (len >= 2 && usb_pid_valid(bytes[1])) ? 1 : 0;
	pid = bytes[pid_index];
	if (!usb_pid_valid(pid) || !usb_pid_is_data(pid))
		return 0;

	payload_with_crc = len - pid_index - 1;
	if (payload_with_crc < REPORT_LEN + USB_PACKET_CRC_LEN)
		return 0;

	*payload = &bytes[pid_index + 1];
	*payload_len = payload_with_crc - USB_PACKET_CRC_LEN;
	return 1;
}

static int feed_bytes(const unsigned char *bytes, size_t len,
		      unsigned char report[REPORT_LEN], size_t *report_pos,
		      struct hidkeys_state *state, bool events)
{
	const unsigned char *payload = NULL;
	size_t payload_len = 0;
	int packet;
	size_t i;

	packet = usb_data_payload(bytes, len, &payload, &payload_len);
	if (packet < 0)
		return packet;
	if (packet > 0) {
		if (*report_pos)
			return -3;
		if (payload_len != REPORT_LEN || payload[1] != 0)
			return -2;
		process_report(state, payload, events);
		return 0;
	}

	for (i = 0; i < len; i++)
		feed_byte(bytes[i], report, report_pos, state, events);
	return 0;
}

static int feed_hex_text(const char *text, unsigned char report[REPORT_LEN],
			 size_t *report_pos, struct hidkeys_state *state,
			 bool events)
{
	unsigned char bytes[INPUT_BYTES_MAX];
	size_t len = 0;

	if (parse_hex_bytes(text, bytes, sizeof(bytes), &len) < 0)
		return -1;
	return feed_bytes(bytes, len, report, report_pos, state, events);
}

static const char *feed_error(int ret)
{
	switch (ret) {
	case -2:
		return "USB DATA packet is not a boot-keyboard report";
	case -3:
		return "USB DATA packet cannot follow a partial raw report";
	default:
		return "bad hex input";
	}
}

int main(int argc, char **argv)
{
	struct hidkeys_state state;
	unsigned char report[REPORT_LEN];
	size_t report_pos = 0;
	bool events = false;
	int arg = 1;

	memset(&state, 0, sizeof(state));

	while (arg < argc && argv[arg][0] == '-') {
		if (!strcmp(argv[arg], "--events") || !strcmp(argv[arg], "-e")) {
			events = true;
		} else if (!strcmp(argv[arg], "--help") ||
			   !strcmp(argv[arg], "-h")) {
			usage(stdout);
			return 0;
		} else {
			usage(stderr);
			fprintf(stderr, "fruitjam-hidkeys: unknown option: %s\n",
				argv[arg]);
			return 2;
		}
		arg++;
	}

	if (arg < argc) {
		for (; arg < argc; arg++) {
			int ret = feed_hex_text(argv[arg], report, &report_pos,
						&state, events);

			if (ret < 0) {
				fprintf(stderr, "fruitjam-hidkeys: %s: %s\n",
					feed_error(ret), argv[arg]);
				return 2;
			}
		}
	} else {
		char line[256];

		while (fgets(line, sizeof(line), stdin)) {
			int ret = feed_hex_text(line, report, &report_pos,
						&state, events);

			if (ret < 0) {
				fprintf(stderr, "fruitjam-hidkeys: %s\n",
					feed_error(ret));
				return 2;
			}
		}
		if (ferror(stdin)) {
			fprintf(stderr, "fruitjam-hidkeys: stdin: %s\n",
				strerror(errno));
			return 1;
		}
	}

	if (report_pos) {
		fprintf(stderr,
			"fruitjam-hidkeys: partial report (%zu of %u bytes)\n",
			report_pos, (unsigned int)REPORT_LEN);
		return 2;
	}

	return 0;
}
