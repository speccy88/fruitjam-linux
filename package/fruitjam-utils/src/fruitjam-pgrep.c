// SPDX-License-Identifier: MIT
/*
 * Tiny /proc pgrep/pkill helper for the Fruit Jam no-MMU image.
 *
 * This intentionally supports simple substring/exact matching instead of a
 * regex engine, avoiding ps|grep|awk|xargs pipelines while services are up.
 */

#define _DEFAULT_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
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
	char comm[64];
	char cmd[CMD_BUF];
};

struct options {
	int full;
	int exact;
	int list;
	int kill_mode;
	int signal_no;
	const char *pattern;
};

static void usage(FILE *out, const char *name)
{
	(void)name;
	fprintf(out,
		"usage: fruitjam-pgrep|pgrep [-f] [-x] [-l] PATTERN\n"
		"       pkill [-f] [-x] [-SIGNAL] PATTERN\n");
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

static int read_proc(unsigned long pid, struct proc_info *info)
{
	char path[96];
	char stat[STAT_BUF];
	char *open_paren;
	char *close_paren;
	size_t comm_len;

	memset(info, 0, sizeof(*info));
	info->pid = pid;
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

	read_cmdline(pid, info->cmd, sizeof(info->cmd));
	if (!info->cmd[0])
		snprintf(info->cmd, sizeof(info->cmd), "[%s]", info->comm);
	return 0;
}

static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

static int parse_signal(const char *arg)
{
	char *end = NULL;
	long value;

	if (!arg || arg[0] != '-' || !isdigit((unsigned char)arg[1]))
		return -1;
	value = strtol(arg + 1, &end, 10);
	if (!end || *end || value <= 0 || value > 64)
		return -1;
	return (int)value;
}

static int parse_args(int argc, char **argv, struct options *opts)
{
	int i;
	const char *name = base_name(argv[0]);

	memset(opts, 0, sizeof(*opts));
	opts->kill_mode = !strcmp(name, "pkill");
	opts->signal_no = SIGTERM;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
			return 1;
		if (!strcmp(arg, "-f")) {
			opts->full = 1;
			continue;
		}
		if (!strcmp(arg, "-x")) {
			opts->exact = 1;
			continue;
		}
		if (!strcmp(arg, "-l")) {
			opts->list = 1;
			continue;
		}
		if (opts->kill_mode && arg[0] == '-') {
			int sig = parse_signal(arg);

			if (sig < 0)
				return -1;
			opts->signal_no = sig;
			continue;
		}
		if (opts->pattern)
			return -1;
		opts->pattern = arg;
	}

	return opts->pattern ? 0 : -1;
}

static int proc_matches(const struct proc_info *info, const struct options *opts)
{
	const char *text = opts->full ? info->cmd : info->comm;

	if (opts->exact)
		return !strcmp(text, opts->pattern);
	return strstr(text, opts->pattern) != NULL;
}

static int send_signal(unsigned long pid, int signal_no)
{
#ifdef FRUITJAM_PGREP_DRY_RUN
	printf("%lu %d\n", pid, signal_no);
	return 0;
#else
	if (pid > (unsigned long)2147483647L) {
		errno = EINVAL;
		return -1;
	}
	return kill((pid_t)pid, signal_no);
#endif
}

int main(int argc, char **argv)
{
	DIR *dir;
	struct dirent *de;
	struct options opts;
	int matched = 0;
	int had_error = 0;
	unsigned long self_pid = (unsigned long)getpid();
	int parsed = parse_args(argc, argv, &opts);

	if (parsed != 0) {
		usage(parsed > 0 ? stdout : stderr, base_name(argv[0]));
		return parsed > 0 ? 0 : 2;
	}

	dir = opendir(PROC_ROOT);
	if (!dir) {
		fprintf(stderr, "%s: cannot open %s: %s\n",
			base_name(argv[0]), PROC_ROOT, strerror(errno));
		return 2;
	}

	while ((de = readdir(dir))) {
		unsigned long pid;
		struct proc_info info;

		if (!is_pid_name(de->d_name, &pid) || pid == self_pid)
			continue;
		if (read_proc(pid, &info) < 0)
			continue;
		if (!proc_matches(&info, &opts))
			continue;

		matched++;
		if (opts.kill_mode) {
			if (send_signal(info.pid, opts.signal_no) < 0) {
				fprintf(stderr, "pkill: %lu: %s\n",
					info.pid, strerror(errno));
				had_error = 1;
			}
		} else if (opts.list) {
			printf("%lu %s\n", info.pid,
			       opts.full ? info.cmd : info.comm);
		} else {
			printf("%lu\n", info.pid);
		}
	}
	closedir(dir);

	if (had_error)
		return 2;
	return matched ? 0 : 1;
}
