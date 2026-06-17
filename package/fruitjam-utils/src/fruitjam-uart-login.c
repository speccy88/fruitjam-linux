// SPDX-License-Identifier: MIT
/*
 * Tiny no-MMU UART login gate.
 *
 * BusyBox init documents askfirst, but on no-MMU it behaves like respawn. When
 * ttyAMA0 returns EOF with nothing attached to the Fruit Jam UART header, that
 * launches and exits hush over and over. Keep one small process alive instead,
 * and exec hush only after a real Enter arrives.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef LOGIN_SHELL
#define LOGIN_SHELL "/usr/bin/hush"
#endif

int main(void)
{
	static const char prompt[] =
		"\nPress Enter to activate the Fruit Jam UART shell. ";
	char c;

	write(STDOUT_FILENO, prompt, sizeof(prompt) - 1);
	for (;;) {
		ssize_t got = read(STDIN_FILENO, &c, 1);

		if (got == 1) {
			if (c == '\n' || c == '\r')
				break;
			continue;
		}
		if (got < 0 && errno != EINTR)
			sleep(1);
		if (got == 0)
			sleep(1);
	}

	execl(LOGIN_SHELL, "hush", (char *)NULL);
	fprintf(stderr, "fruitjam-uart-login: exec %s: %s\n",
		LOGIN_SHELL, strerror(errno));
	sleep(5);
	return 127;
}
