// SPDX-License-Identifier: MIT
/*
 * Tiny /proc process listing for the Fruit Jam no-MMU image.
 *
 * BusyBox ps is disabled to keep the main BusyBox binary small. This helper
 * reads /proc directly and avoids fork-heavy shell pipelines while services
 * are resident.
 */

#define _DEFAULT_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PROC_ROOT
#define PROC_ROOT "/proc"
#endif

#define STAT_BUF 512
#define CMD_BUF 160

struct proc_info {
	unsigned long pid;
	unsigned long ppid;
	char state;
	unsigned long vsize_kb;
	long rss_kb;
	char comm[64];
	char cmd[CMD_BUF];
};

static void usage(FILE *out)
{
	fprintf(out, "usage: fruitjam-ps [json]\n");
}

static int proc_path(unsigned long pid, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/%lu/%s", PROC_ROOT, pid, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int is_pid_name(const char *name, unsigned long *pid)
{
	unsigned long value = 0;
	const char *p;

	if (!name || !*name)
		return 0;
	for (p = name; *p; p++) {
		if (!isdigit((unsigned char)*p))
			return 0;
		value = value * 10 + (unsigned long)(*p - '0');
	}
	*pid = value;
	return 1;
}

static int read_first_line(const char *path, char *buf, size_t len)
{
	FILE *fp;
	size_t used;

	if (!len)
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

static void read_cmdline(unsigned long pid, char *buf, size_t len)
{
	char path[96];
	FILE *fp;
	size_t n;
	size_t i;

	if (!len)
		return;
	buf[0] = '\0';
	if (proc_path(pid, "cmdline", path, sizeof(path)) < 0)
		return;
	fp = fopen(path, "r");
	if (!fp)
		return;
	n = fread(buf, 1, len - 1, fp);
	fclose(fp);
	if (!n) {
		buf[0] = '\0';
		return;
	}
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)buf[i];

		if (c == '\0')
			buf[i] = ' ';
		else if (c < 0x20 || c >= 0x7f)
			buf[i] = '?';
	}
	while (n && buf[n - 1] == ' ')
		n--;
	buf[n] = '\0';
}

static int parse_stat_tokens(char *rest, struct proc_info *info)
{
	char *tok;
	unsigned int field = 3;
	unsigned long long vsize = 0;
	long rss_pages = 0;
	long page_kb = sysconf(_SC_PAGESIZE) / 1024;

	if (page_kb <= 0)
		page_kb = 4;
	tok = strtok(rest, " \t");
	while (tok) {
		if (field == 3)
			info->state = tok[0];
		else if (field == 4)
			info->ppid = strtoul(tok, NULL, 10);
		else if (field == 23)
			vsize = strtoull(tok, NULL, 10);
		else if (field == 24) {
			rss_pages = strtol(tok, NULL, 10);
			break;
		}
		field++;
		tok = strtok(NULL, " \t");
	}
	if (field < 24)
		return -1;
	info->vsize_kb = (unsigned long)(vsize / 1024ULL);
	info->rss_kb = rss_pages < 0 ? 0 : rss_pages * page_kb;
	return 0;
}

static int read_proc(unsigned long pid, struct proc_info *info)
{
	char path[96];
	char stat[STAT_BUF];
	char *open_paren;
	char *close_paren;
	size_t comm_len;

	memset(info, 0, sizeof(*info));
	info->pid = pid;
	info->state = '?';
	if (proc_path(pid, "stat", path, sizeof(path)) < 0 ||
	    read_first_line(path, stat, sizeof(stat)) < 0)
		return -1;

	open_paren = strchr(stat, '(');
	close_paren = strrchr(stat, ')');
	if (!open_paren || !close_paren || close_paren <= open_paren)
		return -1;
	comm_len = (size_t)(close_paren - open_paren - 1);
	if (comm_len >= sizeof(info->comm))
		comm_len = sizeof(info->comm) - 1;
	memcpy(info->comm, open_paren + 1, comm_len);
	info->comm[comm_len] = '\0';
	if (parse_stat_tokens(close_paren + 2, info) < 0)
		return -1;

	read_cmdline(pid, info->cmd, sizeof(info->cmd));
	if (!info->cmd[0])
		snprintf(info->cmd, sizeof(info->cmd), "[%s]", info->comm);
	return 0;
}

static void json_string(const char *s)
{
	putchar('"');
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '\\')
			fputs("\\\\", stdout);
		else if (c == '"')
			fputs("\\\"", stdout);
		else if (c == '\n')
			fputs("\\n", stdout);
		else if (c == '\r')
			fputs("\\r", stdout);
		else if (c == '\t')
			fputs("\\t", stdout);
		else if (c < 0x20)
			printf("\\u%04x", c);
		else
			putchar(c);
	}
	putchar('"');
}

static void print_text_header(void)
{
	puts("  PID  PPID S      VSZ      RSS COMMAND");
}

static void print_text_proc(const struct proc_info *p)
{
	printf("%5lu %5lu %c %8lu %8ld %s\n", p->pid, p->ppid, p->state,
	       p->vsize_kb, p->rss_kb, p->cmd);
}

static void print_json_proc(const struct proc_info *p, int first)
{
	if (!first)
		putchar(',');
	printf("{\"pid\":%lu,\"ppid\":%lu,\"state\":", p->pid, p->ppid);
	json_string((char[]){ p->state, '\0' });
	printf(",\"vsize_kb\":%lu,\"rss_kb\":%ld,\"command\":",
	       p->vsize_kb, p->rss_kb);
	json_string(p->cmd);
	putchar('}');
}

int main(int argc, char **argv)
{
	DIR *dir;
	struct dirent *de;
	int json = 0;
	int first = 1;

	if (argc > 1) {
		if (!strcmp(argv[1], "json")) {
			json = 1;
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(stdout);
			return 0;
		} else {
			usage(stderr);
			return 1;
		}
	}

	dir = opendir(PROC_ROOT);
	if (!dir) {
		fprintf(stderr, "fruitjam-ps: cannot open %s: %s\n",
			PROC_ROOT, strerror(errno));
		return 1;
	}

	if (json)
		fputs("{\"ok\":true,\"processes\":[", stdout);
	else
		print_text_header();
	while ((de = readdir(dir))) {
		unsigned long pid;
		struct proc_info info;

		if (!is_pid_name(de->d_name, &pid))
			continue;
		if (read_proc(pid, &info) < 0)
			continue;
		if (json)
			print_json_proc(&info, first);
		else
			print_text_proc(&info);
		first = 0;
	}
	closedir(dir);
	if (json)
		puts("]}");
	return 0;
}
