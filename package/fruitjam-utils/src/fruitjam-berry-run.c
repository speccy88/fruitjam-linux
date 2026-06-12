// SPDX-License-Identifier: MIT
/*
 * Tiny Berry launcher for the no-MMU Fruit Jam image.
 *
 * Berry itself is close to a 200 KiB flat binary allocation. Dropping page
 * cache from a small helper before exec keeps the example runner usable after
 * HTTP, FTP, SD, and audio tests have warmed the cache.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BERRY_BIN "/usr/bin/berry"

static void usage(void)
{
	fprintf(stderr, "usage: berry-run [-e script] [script.be]\n");
}

static void drop_page_cache(void)
{
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);

	if (fd < 0)
		return;
	write(fd, "3\n", 2);
	close(fd);
}

int main(int argc, char **argv)
{
	if (argc > 3 || (argc == 2 && (!strcmp(argv[1], "-h") ||
				       !strcmp(argv[1], "--help")))) {
		usage();
		return argc > 3 ? 1 : 0;
	}

	drop_page_cache();
	argv[0] = (char *)"berry";
	execv(BERRY_BIN, argv);
	fprintf(stderr, "berry-run: exec %s: %s\n", BERRY_BIN, strerror(errno));
	return 127;
}
