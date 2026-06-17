// SPDX-License-Identifier: MIT
/*
 * Tiny no-fork memory/uptime helper for the Fruit Jam no-MMU image.
 *
 * BusyBox procps applets are deliberately kept out of the main BusyBox binary.
 * This gives users a familiar small "free"-style view without spawning awk,
 * ps, top, or a large applet while services are resident.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PROC_ROOT
#define PROC_ROOT "/proc"
#endif

struct meminfo {
	long mem_total;
	long mem_free;
	long mem_available;
	long buffers;
	long cached;
	long sreclaimable;
	long shmem;
	long swap_total;
	long swap_free;
	long commit_limit;
	long committed_as;
};

static void usage(FILE *out)
{
	fprintf(out, "usage: fruitjam-mem [json]\n");
}

static void init_meminfo(struct meminfo *m)
{
	m->mem_total = -1;
	m->mem_free = -1;
	m->mem_available = -1;
	m->buffers = -1;
	m->cached = -1;
	m->sreclaimable = -1;
	m->shmem = -1;
	m->swap_total = -1;
	m->swap_free = -1;
	m->commit_limit = -1;
	m->committed_as = -1;
}

static int proc_path(const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/%s", PROC_ROOT, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int read_line(const char *leaf, char *buf, size_t len)
{
	char path[96];
	FILE *fp;
	size_t used;

	if (proc_path(leaf, path, sizeof(path)) < 0 || len == 0)
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
	while (used > 0 && (buf[used - 1] == '\n' || buf[used - 1] == '\r'))
		buf[--used] = '\0';
	return 0;
}

static void set_mem_value(struct meminfo *m, const char *key, long value)
{
	if (!strcmp(key, "MemTotal"))
		m->mem_total = value;
	else if (!strcmp(key, "MemFree"))
		m->mem_free = value;
	else if (!strcmp(key, "MemAvailable"))
		m->mem_available = value;
	else if (!strcmp(key, "Buffers"))
		m->buffers = value;
	else if (!strcmp(key, "Cached"))
		m->cached = value;
	else if (!strcmp(key, "SReclaimable"))
		m->sreclaimable = value;
	else if (!strcmp(key, "Shmem"))
		m->shmem = value;
	else if (!strcmp(key, "SwapTotal"))
		m->swap_total = value;
	else if (!strcmp(key, "SwapFree"))
		m->swap_free = value;
	else if (!strcmp(key, "CommitLimit"))
		m->commit_limit = value;
	else if (!strcmp(key, "Committed_AS"))
		m->committed_as = value;
}

static int read_meminfo(struct meminfo *m)
{
	char path[96];
	char line[128];
	FILE *fp;

	if (proc_path("meminfo", path, sizeof(path)) < 0)
		return -1;
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		char key[32];
		long value;

		if (sscanf(line, "%31[^:]: %ld", key, &value) == 2)
			set_mem_value(m, key, value);
	}
	fclose(fp);
	return m->mem_total >= 0 ? 0 : -1;
}

static long value_or_zero(long value)
{
	return value < 0 ? 0 : value;
}

static long cache_kb(const struct meminfo *m)
{
	long cache = value_or_zero(m->cached) + value_or_zero(m->sreclaimable) -
		value_or_zero(m->shmem);

	return cache < 0 ? 0 : cache;
}

static long used_kb(const struct meminfo *m, long cache)
{
	long used = m->mem_total - value_or_zero(m->mem_free) -
		value_or_zero(m->buffers) - cache;

	return used < 0 ? 0 : used;
}

static void print_kv(const char *key, long value)
{
	if (value < 0)
		printf(",\"%s\":null", key);
	else
		printf(",\"%s\":%ld", key, value);
}

static void print_json_number(const char *key, const char *line, unsigned int index)
{
	char copy[96];
	char *tok;
	unsigned int i;

	if (!line || !*line) {
		printf(",\"%s\":null", key);
		return;
	}
	snprintf(copy, sizeof(copy), "%s", line);
	tok = strtok(copy, " \t");
	for (i = 0; tok && i < index; i++)
		tok = strtok(NULL, " \t");
	if (!tok)
		printf(",\"%s\":null", key);
	else
		printf(",\"%s\":%s", key, tok);
}

static void print_json(const struct meminfo *m, const char *uptime, const char *loadavg)
{
	long cache = cache_kb(m);

	printf("{\"ok\":true,\"unit\":\"kB\",\"mem\":{");
	printf("\"total\":%ld", m->mem_total);
	print_kv("used", used_kb(m, cache));
	print_kv("free", m->mem_free);
	print_kv("shared", m->shmem);
	print_kv("buffers", m->buffers);
	print_kv("cache", cache);
	print_kv("available", m->mem_available);
	printf("},\"swap\":{");
	printf("\"total\":%ld", value_or_zero(m->swap_total));
	print_kv("used", value_or_zero(m->swap_total) - value_or_zero(m->swap_free));
	print_kv("free", m->swap_free);
	printf("}");
	if (m->commit_limit >= 0 || m->committed_as >= 0) {
		printf(",\"commit\":{");
		printf("\"limit\":%ld", value_or_zero(m->commit_limit));
		print_kv("committed", m->committed_as);
		printf("}");
	}
	print_json_number("uptime_seconds", uptime, 0);
	print_json_number("load_1m", loadavg, 0);
	print_json_number("load_5m", loadavg, 1);
	print_json_number("load_15m", loadavg, 2);
	printf("}\n");
}

static void print_text(const struct meminfo *m, const char *uptime, const char *loadavg)
{
	long cache = cache_kb(m);
	long swap_total = value_or_zero(m->swap_total);
	long swap_free = value_or_zero(m->swap_free);

	puts("Fruit Jam memory (kB)");
	puts("              total        used        free      shared     buffers       cache   available");
	printf("Mem:    %11ld %11ld %11ld %11ld %11ld %11ld %11ld\n",
	       m->mem_total, used_kb(m, cache), value_or_zero(m->mem_free),
	       value_or_zero(m->shmem), value_or_zero(m->buffers), cache,
	       value_or_zero(m->mem_available));
	printf("Swap:   %11ld %11ld %11ld\n",
	       swap_total, swap_total - swap_free, swap_free);
	if (m->commit_limit >= 0 || m->committed_as >= 0)
		printf("Commit: limit %ld committed %ld\n",
		       value_or_zero(m->commit_limit), value_or_zero(m->committed_as));
	if (uptime && *uptime)
		printf("Uptime: %s\n", uptime);
	if (loadavg && *loadavg)
		printf("Load:   %s\n", loadavg);
	puts("Note: no-MMU allocation failures can still happen before free memory reaches zero.");
}

int main(int argc, char **argv)
{
	struct meminfo m;
	char uptime[96];
	char loadavg[96];
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

	init_meminfo(&m);
	if (read_meminfo(&m) < 0) {
		fprintf(stderr, "fruitjam-mem: cannot read %s/meminfo: %s\n",
			PROC_ROOT, strerror(errno));
		return 1;
	}
	if (read_line("uptime", uptime, sizeof(uptime)) < 0)
		uptime[0] = '\0';
	if (read_line("loadavg", loadavg, sizeof(loadavg)) < 0)
		loadavg[0] = '\0';

	if (json)
		print_json(&m, uptime, loadavg);
	else
		print_text(&m, uptime, loadavg);
	return 0;
}
