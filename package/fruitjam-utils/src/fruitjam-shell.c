// SPDX-License-Identifier: MIT
/*
 * Tiny interactive shell for telnetd on the no-MMU Fruit Jam image.
 *
 * It intentionally implements only the small command loop needed for remote
 * bring-up. Serial consoles still use hush; telnet uses this smaller shell so
 * accepting a remote session does not require a 128 KiB contiguous allocation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const char *path_dirs[] = {
	"/bin", "/usr/bin", "/sbin", "/usr/sbin"
};

static void prompt(void)
{
	fputs("fj$ ", stdout);
	fflush(stdout);
}

static char *trim(char *s)
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

static int read_interactive_line(char *line, size_t line_size)
{
	if (line_size == 0)
		return 0;

	/*
	 * Keep the remote shell line-oriented. Echoing and flushing every byte is
	 * very slow through the AirLift bridge and wastes scarce no-MMU memory on
	 * in-flight socket traffic.
	 */
	if (!fgets(line, line_size, stdin)) {
		line[0] = '\0';
		return 0;
	}
	return 1;
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

int main(void)
{
	char line[256];
	int last_status = 0;

	puts("Fruit Jam telnet shell");
	for (;;) {
		char *cmd;
		char *argv[16];
		int argc;

		prompt();
		if (!read_interactive_line(line, sizeof(line)))
			break;
		cmd = trim(line);
		if (!*cmd)
			continue;

		argc = split_args(cmd, argv, ARRAY_SIZE(argv));
		if (argc == 0)
			continue;
		if (!strcmp(argv[0], "exit"))
			break;
		if (!strcmp(argv[0], "echo")) {
			int i;

			for (i = 1; i < argc; i++) {
				if (i > 1)
					putchar(' ');
				fputs(argv[i], stdout);
			}
			putchar('\n');
			last_status = 0;
			continue;
		}
		if (!strcmp(argv[0], "cd")) {
			const char *dir = argc > 1 ? argv[1] : "/";
			if (chdir(dir) < 0) {
				perror(dir);
				last_status = 1;
			} else {
				last_status = 0;
			}
			continue;
		}
		if (!strcmp(argv[0], "?") || !strcmp(argv[0], "help")) {
			puts("builtins: cd echo exit help status");
			puts("simple commands are searched in /bin /usr/bin /sbin /usr/sbin");
			last_status = 0;
			continue;
		}
		if (!strcmp(argv[0], "status")) {
			printf("%d\n", last_status);
			continue;
		}

		last_status = run_command(argv);
	}

	return last_status;
}
