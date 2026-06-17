// SPDX-License-Identifier: MIT
/*
 * Tiny /proc uptime/load helper for the Fruit Jam no-MMU image.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PROC_ROOT
#define PROC_ROOT "/proc"
#endif

struct uptime_info {
	unsigned long uptime_seconds;
	unsigned long idle_seconds;
	char load_1m[16];
	char load_5m[16];
	char load_15m[16];
};

static void usage(FILE *out)
{
	fprintf(out, "usage: fruitjam-uptime [json]\n");
}

static int proc_path(const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/%s", PROC_ROOT, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int read_first_line(const char *leaf, char *buf, size_t len)
{
	char path[96];
	FILE *fp;
	size_t used;

	if (!len || proc_path(leaf, path, sizeof(path)) < 0)
		return -1;
	fp = fopen(path, "r");
	if (!fp) {
		buf[0] = '\0';
		return -1;
	}
	if (!fgets(buf, len, fp)) {
		fclose(fp);
		buf[0] = '\0';
		return -1;
	}
	fclose(fp);
	used = strlen(buf);
	while (used && (buf[used - 1] == '\n' || buf[used - 1] == '\r'))
		buf[--used] = '\0';
	return 0;
}

static unsigned long parse_seconds_token(const char *token)
{
	char buf[32];
	size_t i = 0;

	while (token[i] && token[i] != '.' && i + 1 < sizeof(buf)) {
		buf[i] = token[i];
		i++;
	}
	buf[i] = '\0';
	return strtoul(buf, NULL, 10);
}

static int read_uptime(struct uptime_info *info)
{
	char uptime[96];
	char loadavg[96];
	char up_tok[32];
	char idle_tok[32];

	memset(info, 0, sizeof(*info));
	strcpy(info->load_1m, "0.00");
	strcpy(info->load_5m, "0.00");
	strcpy(info->load_15m, "0.00");

	if (read_first_line("uptime", uptime, sizeof(uptime)) < 0)
		return -1;
	if (sscanf(uptime, "%31s %31s", up_tok, idle_tok) < 1)
		return -1;
	info->uptime_seconds = parse_seconds_token(up_tok);
	info->idle_seconds = parse_seconds_token(idle_tok);

	if (read_first_line("loadavg", loadavg, sizeof(loadavg)) == 0)
		sscanf(loadavg, "%15s %15s %15s",
		       info->load_1m, info->load_5m, info->load_15m);
	return 0;
}

static void format_duration(unsigned long seconds, char *buf, size_t len)
{
	unsigned long days = seconds / 86400UL;
	unsigned long rem = seconds % 86400UL;
	unsigned long hours = rem / 3600UL;
	unsigned long mins = (rem % 3600UL) / 60UL;

	if (days)
		snprintf(buf, len, "%lu day%s, %02lu:%02lu",
			 days, days == 1 ? "" : "s", hours, mins);
	else
		snprintf(buf, len, "%02lu:%02lu", hours, mins);
}

static void print_text(const struct uptime_info *info)
{
	char duration[48];

	format_duration(info->uptime_seconds, duration, sizeof(duration));
	printf("up %s, load average: %s, %s, %s\n",
	       duration, info->load_1m, info->load_5m, info->load_15m);
}

static void print_json(const struct uptime_info *info)
{
	printf("{\"ok\":true,\"uptime_seconds\":%lu,\"idle_seconds\":%lu,"
	       "\"load_1m\":%s,\"load_5m\":%s,\"load_15m\":%s}\n",
	       info->uptime_seconds, info->idle_seconds,
	       info->load_1m, info->load_5m, info->load_15m);
}

int main(int argc, char **argv)
{
	struct uptime_info info;
	int json = 0;

	if (argc > 2) {
		usage(stderr);
		return 1;
	}
	if (argc == 2) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(stdout);
			return 0;
		}
		if (strcmp(argv[1], "json")) {
			usage(stderr);
			return 1;
		}
		json = 1;
	}

	if (read_uptime(&info) < 0) {
		fprintf(stderr, "fruitjam-uptime: cannot read %s/uptime: %s\n",
			PROC_ROOT, strerror(errno));
		return 1;
	}

	if (json)
		print_json(&info);
	else
		print_text(&info);
	return 0;
}
