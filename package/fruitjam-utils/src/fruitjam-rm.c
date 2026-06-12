// SPDX-License-Identifier: MIT
/*
 * Tiny rm for the Fruit Jam no-MMU image.
 *
 * BusyBox-derived standalone applet builds can fail as "applet not found" when
 * argv[0] and the generated applet table disagree. Keep rm as a direct helper
 * because shell examples and interactive cleanup need it to be boring.
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_BUF 512

static int opt_force;
static int opt_recursive;

static void usage(void)
{
	fprintf(stderr, "usage: rm [-f] [-r|-R] FILE...\n");
}

static int join_path(char *out, size_t out_len, const char *dir, const char *name)
{
	int ret = snprintf(out, out_len, "%s/%s", dir, name);

	if (ret < 0 || (size_t)ret >= out_len) {
		fprintf(stderr, "rm: path too long: %s/%s\n", dir, name);
		return -1;
	}
	return 0;
}

static int remove_path(const char *path)
{
	struct stat st;

	if (lstat(path, &st) < 0) {
		if (!opt_force)
			fprintf(stderr, "rm: cannot remove '%s': %s\n",
				path, strerror(errno));
		return opt_force && errno == ENOENT ? 0 : -1;
	}

	if (S_ISDIR(st.st_mode)) {
		DIR *dir;
		struct dirent *de;
		int ret = 0;

		if (!opt_recursive) {
			fprintf(stderr, "rm: cannot remove '%s': is a directory\n",
				path);
			return -1;
		}

		dir = opendir(path);
		if (!dir) {
			if (!opt_force)
				fprintf(stderr, "rm: cannot open '%s': %s\n",
					path, strerror(errno));
			return -1;
		}

		while ((de = readdir(dir)) != NULL) {
			char child[PATH_BUF];

			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			if (join_path(child, sizeof(child), path, de->d_name) < 0 ||
			    remove_path(child) < 0)
				ret = -1;
		}
		closedir(dir);

		if (rmdir(path) < 0) {
			if (!opt_force)
				fprintf(stderr, "rm: cannot remove '%s': %s\n",
					path, strerror(errno));
			ret = -1;
		}
		return ret;
	}

	if (unlink(path) < 0) {
		if (!opt_force)
			fprintf(stderr, "rm: cannot remove '%s': %s\n",
				path, strerror(errno));
		return -1;
	}
	return 0;
}

static int parse_options(const char *arg)
{
	size_t i;

	if (!strcmp(arg, "--"))
		return 1;
	if (arg[0] != '-' || !arg[1])
		return 0;
	for (i = 1; arg[i]; i++) {
		if (arg[i] == 'f')
			opt_force = 1;
		else if (arg[i] == 'r' || arg[i] == 'R')
			opt_recursive = 1;
		else {
			fprintf(stderr, "rm: bad option -- %c\n", arg[i]);
			return -1;
		}
	}
	return 1;
}

int main(int argc, char **argv)
{
	int i;
	int first_path;
	int ret = 0;

	for (i = 1; i < argc; i++) {
		int parsed = parse_options(argv[i]);

		if (parsed < 0) {
			usage();
			return 1;
		}
		if (!parsed)
			break;
		if (!strcmp(argv[i], "--")) {
			i++;
			break;
		}
	}
	first_path = i;
	if (first_path >= argc) {
		if (!opt_force) {
			usage();
			return 1;
		}
		return 0;
	}

	for (i = first_path; i < argc; i++) {
		if (remove_path(argv[i]) < 0)
			ret = 1;
	}
	return ret;
}
