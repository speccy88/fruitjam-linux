// SPDX-License-Identifier: MIT
/*
 * Tiny recursive disk-usage helper for the Fruit Jam no-MMU image.
 */

#define _DEFAULT_SOURCE

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PATH_BUF 512

struct options {
	int summarize;
	int all;
	int bytes;
	int json;
};

struct walk_result {
	unsigned long long blocks_512;
	unsigned long long bytes;
	int errors;
};

static void usage(FILE *out)
{
	fprintf(out, "usage: fruitjam-du [-s] [-a] [-b] [json] [PATH...]\n");
}

static int join_path(const char *parent, const char *name, char *out, size_t len)
{
	int ret;

	if (!strcmp(parent, "/"))
		ret = snprintf(out, len, "/%s", name);
	else
		ret = snprintf(out, len, "%s/%s", parent, name);
	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static unsigned long long stat_blocks_512(const struct stat *st)
{
	unsigned long long size = (unsigned long long)st->st_size;

	if (st->st_blocks > 0)
		return (unsigned long long)st->st_blocks;
	return (size + 511ULL) / 512ULL;
}

static unsigned long long units_from_blocks(unsigned long long blocks_512,
					    int bytes)
{
	if (bytes)
		return blocks_512 * 512ULL;
	return (blocks_512 + 1ULL) / 2ULL;
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

static void print_entry(const char *path, const struct walk_result *res,
			const struct options *opts, int *first_json)
{
	unsigned long long value = units_from_blocks(res->blocks_512, opts->bytes);

	if (opts->json) {
		if (!*first_json)
			putchar(',');
		printf("{\"path\":");
		json_string(path);
		printf(",\"%s\":%llu", opts->bytes ? "bytes" : "kb", value);
		if (!opts->bytes)
			printf(",\"bytes\":%llu", res->bytes);
		printf("}");
		*first_json = 0;
	} else {
		printf("%llu\t%s\n", value, path);
	}
}

static struct walk_result walk_path(const char *path, const struct options *opts,
				    int top, int *first_json)
{
	struct walk_result total = { 0, 0, 0 };
	struct stat st;

	if (lstat(path, &st) < 0) {
		fprintf(stderr, "du: %s: %s\n", path, strerror(errno));
		total.errors = 1;
		return total;
	}

	total.blocks_512 = stat_blocks_512(&st);
	total.bytes = (unsigned long long)st.st_size;

	if (S_ISDIR(st.st_mode)) {
		DIR *dir = opendir(path);
		struct dirent *de;

		if (!dir) {
			fprintf(stderr, "du: %s: %s\n", path, strerror(errno));
			total.errors = 1;
		} else {
			while ((de = readdir(dir))) {
				char child[PATH_BUF];
				struct walk_result child_res;

				if (!strcmp(de->d_name, ".") ||
				    !strcmp(de->d_name, ".."))
					continue;
				if (join_path(path, de->d_name, child,
					      sizeof(child)) < 0) {
					fprintf(stderr, "du: path too long: %s/%s\n",
						path, de->d_name);
					total.errors = 1;
					continue;
				}
				child_res = walk_path(child, opts, 0, first_json);
				total.blocks_512 += child_res.blocks_512;
				total.bytes += child_res.bytes;
				total.errors |= child_res.errors;
			}
			closedir(dir);
		}
	}

	if (top || (!opts->summarize && (opts->all || S_ISDIR(st.st_mode))))
		print_entry(path, &total, opts, first_json);
	return total;
}

int main(int argc, char **argv)
{
	struct options opts = { 0, 0, 0, 0 };
	int i;
	int paths = 0;
	int first_json = 1;
	int errors = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			usage(stdout);
			return 0;
		}
		if (!strcmp(arg, "-s")) {
			opts.summarize = 1;
			continue;
		}
		if (!strcmp(arg, "-a")) {
			opts.all = 1;
			continue;
		}
		if (!strcmp(arg, "-b")) {
			opts.bytes = 1;
			continue;
		}
		if (!strcmp(arg, "json")) {
			opts.json = 1;
			continue;
		}
		break;
	}

	if (opts.json)
		printf("{\"ok\":true,\"unit\":\"%s\",\"entries\":[",
		       opts.bytes ? "bytes" : "kB");

	for (; i < argc; i++) {
		struct walk_result res = walk_path(argv[i], &opts, 1,
						   &first_json);

		paths++;
		errors |= res.errors;
	}
	if (!paths) {
		struct walk_result res = walk_path(".", &opts, 1, &first_json);

		errors |= res.errors;
	}

	if (opts.json)
		printf("]}\n");
	return errors ? 1 : 0;
}
