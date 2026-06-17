// SPDX-License-Identifier: MIT
/*
 * Tiny interactive shell for telnetd on the no-MMU Fruit Jam image.
 *
 * It intentionally implements only the small command loop needed for remote
 * bring-up. Serial consoles still use hush; telnet uses this smaller shell so
 * accepting a remote session does not require a 128 KiB contiguous allocation.
 */

#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define LINE_SIZE 256
#define HISTORY_DEPTH 4

static const char *path_dirs[] = {
	"/bin", "/usr/bin", "/sbin", "/usr/sbin"
};

static const char *builtins[] = {
	"cd", "echo", "exit", "help", "history", "status", "?"
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

static void redraw_line(const char *line)
{
	fputs("\r\033[K", stdout);
	prompt();
	fputs(line, stdout);
	fflush(stdout);
}

static int append_text(char *line, size_t *len, size_t line_size,
		       const char *text)
{
	while (*text && *len + 1 < line_size) {
		line[(*len)++] = *text++;
		putchar(line[*len - 1]);
	}
	line[*len] = '\0';
	fflush(stdout);
	return *text == '\0';
}

static void update_common_prefix(char *common, const char *name)
{
	size_t i = 0;

	while (common[i] && name[i] && common[i] == name[i])
		i++;
	common[i] = '\0';
}

static void note_completion_match(const char *name, const char *prefix,
				  char *common, size_t common_size,
				  int *matches)
{
	size_t prefix_len = strlen(prefix);

	if (strncmp(name, prefix, prefix_len))
		return;
	if (*matches > 0 && !strcmp(common, name))
		return;
	if (*matches == 0) {
		snprintf(common, common_size, "%s", name);
	} else {
		update_common_prefix(common, name);
	}
	(*matches)++;
}

static int path_is_dir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void note_path_match(const char *dir_prefix, const char *name,
			    const char *token, char *common,
			    size_t common_size, int *matches)
{
	char candidate[LINE_SIZE];
	size_t len;
	int ret;

	ret = snprintf(candidate, sizeof(candidate), "%s%s", dir_prefix, name);
	if (ret <= 0 || (size_t)ret >= sizeof(candidate))
		return;
	len = (size_t)ret;
	if (path_is_dir(candidate) && len + 1 < sizeof(candidate)) {
		candidate[len++] = '/';
		candidate[len] = '\0';
	}
	note_completion_match(candidate, token, common, common_size, matches);
}

static int complete_command(char *line, size_t *len, size_t line_size)
{
	char prefix[64];
	char common[64];
	size_t i;
	int matches = 0;

	for (i = 0; i < *len; i++) {
		if (line[i] == ' ' || line[i] == '\t') {
			putchar('\a');
			fflush(stdout);
			return 0;
		}
	}
	if (*len >= sizeof(prefix)) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	memcpy(prefix, line, *len);
	prefix[*len] = '\0';
	common[0] = '\0';

	for (i = 0; i < ARRAY_SIZE(builtins); i++)
		note_completion_match(builtins[i], prefix, common,
				      sizeof(common), &matches);
	for (i = 0; i < ARRAY_SIZE(path_dirs); i++) {
		DIR *dir = opendir(path_dirs[i]);
		struct dirent *de;

		if (!dir)
			continue;
		while ((de = readdir(dir)))
			note_completion_match(de->d_name, prefix, common,
					      sizeof(common), &matches);
		closedir(dir);
	}

	if (matches == 0) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	if (strlen(common) > *len)
		append_text(line, len, line_size, common + *len);
	if (matches == 1 && *len + 1 < line_size)
		append_text(line, len, line_size, " ");
	else if (matches > 1 && strlen(common) == *len)
		putchar('\a');
	fflush(stdout);
	return 1;
}

static int complete_path(char *line, size_t *len, size_t line_size,
			 size_t token_start)
{
	char token[LINE_SIZE];
	char dir[LINE_SIZE];
	char dir_prefix[LINE_SIZE];
	char common[LINE_SIZE];
	const char *name_prefix;
	char *slash;
	DIR *dh;
	struct dirent *de;
	size_t token_len = *len - token_start;
	int matches = 0;

	if (token_len >= sizeof(token)) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	memcpy(token, line + token_start, token_len);
	token[token_len] = '\0';

	slash = strrchr(token, '/');
	if (slash) {
		size_t prefix_len = (size_t)(slash - token) + 1;

		if (prefix_len >= sizeof(dir_prefix)) {
			putchar('\a');
			fflush(stdout);
			return 0;
		}
		memcpy(dir_prefix, token, prefix_len);
		dir_prefix[prefix_len] = '\0';
		if (prefix_len == 1 && token[0] == '/') {
			snprintf(dir, sizeof(dir), "/");
		} else {
			memcpy(dir, token, prefix_len - 1);
			dir[prefix_len - 1] = '\0';
		}
		name_prefix = slash + 1;
	} else {
		snprintf(dir, sizeof(dir), ".");
		dir_prefix[0] = '\0';
		name_prefix = token;
	}

	dh = opendir(dir);
	if (!dh) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	common[0] = '\0';
	while ((de = readdir(dh))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (strncmp(de->d_name, name_prefix, strlen(name_prefix)))
			continue;
		note_path_match(dir_prefix, de->d_name, token, common,
				sizeof(common), &matches);
	}
	closedir(dh);

	if (matches == 0) {
		putchar('\a');
		fflush(stdout);
		return 0;
	}
	if (strlen(common) > token_len)
		append_text(line, len, line_size, common + token_len);
	if (matches == 1 && *len > 0 && line[*len - 1] != '/' &&
	    *len + 1 < line_size)
		append_text(line, len, line_size, " ");
	else if (matches > 1 && strlen(common) == token_len)
		putchar('\a');
	fflush(stdout);
	return 1;
}

static int complete_line(char *line, size_t *len, size_t line_size)
{
	size_t token_start = *len;

	while (token_start > 0 &&
	       line[token_start - 1] != ' ' &&
	       line[token_start - 1] != '\t')
		token_start--;
	if (token_start == 0)
		return complete_command(line, len, line_size);
	return complete_path(line, len, line_size, token_start);
}

static void add_history(char history[HISTORY_DEPTH][LINE_SIZE],
			int *history_count, const char *cmd)
{
	if (!*cmd)
		return;
	if (*history_count > 0 &&
	    !strcmp(history[*history_count - 1], cmd))
		return;
	if (*history_count == HISTORY_DEPTH) {
		memmove(history[0], history[1],
			(HISTORY_DEPTH - 1) * LINE_SIZE);
		(*history_count)--;
	}
	snprintf(history[*history_count], LINE_SIZE, "%s", cmd);
	(*history_count)++;
}

static int read_byte_timeout(unsigned char *c, int timeout_ms)
{
	struct timeval tv;
	fd_set rfds;
	int ret;

	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
	if (ret <= 0)
		return 0;
	return read(STDIN_FILENO, c, 1) == 1;
}

static int read_plain_line(char *line, size_t line_size)
{
	if (line_size == 0)
		return 0;
	if (!fgets(line, line_size, stdin)) {
		line[0] = '\0';
		return 0;
	}
	return 1;
}

static int read_interactive_line(char *line, size_t line_size,
				 char history[HISTORY_DEPTH][LINE_SIZE],
				 int history_count)
{
	struct termios oldt, raw;
	size_t len = 0;
	int history_pos = history_count;

	if (tcgetattr(STDIN_FILENO, &oldt) < 0)
		return read_plain_line(line, line_size);

	raw = oldt;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
		return read_plain_line(line, line_size);

	line[0] = '\0';
	for (;;) {
		unsigned char c;

		if (read(STDIN_FILENO, &c, 1) != 1) {
			tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
			line[0] = '\0';
			return 0;
		}
		if (c == '\r' || c == '\n') {
			putchar('\n');
			line[len] = '\0';
			tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
			return 1;
		}
		if (c == 4 && len == 0) {
			tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
			line[0] = '\0';
			return 0;
		}
		if (c == 3) {
			fputs("^C\n", stdout);
			line[0] = '\0';
			tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
			return 1;
		}
		if ((c == 8 || c == 127) && len > 0) {
			len--;
			line[len] = '\0';
			fputs("\b \b", stdout);
			fflush(stdout);
			history_pos = history_count;
			continue;
		}
		if (c == '\t') {
			complete_line(line, &len, line_size);
			history_pos = history_count;
			continue;
		}
		if (c == 27) {
			unsigned char b, d;

			if (!read_byte_timeout(&b, 80) || b != '[' ||
			    !read_byte_timeout(&d, 80))
				continue;
			if (d == 'A' && history_count > 0) {
				if (history_pos > 0)
					history_pos--;
				snprintf(line, line_size, "%s",
					 history[history_pos]);
				len = strlen(line);
				redraw_line(line);
			} else if (d == 'B' && history_count > 0) {
				if (history_pos < history_count - 1) {
					history_pos++;
					snprintf(line, line_size, "%s",
						 history[history_pos]);
				} else {
					history_pos = history_count;
					line[0] = '\0';
				}
				len = strlen(line);
				redraw_line(line);
			}
			continue;
		}
		if (isprint(c) && len + 1 < line_size) {
			line[len++] = c;
			line[len] = '\0';
			putchar(c);
			fflush(stdout);
			history_pos = history_count;
		}
	}
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
	char history[HISTORY_DEPTH][LINE_SIZE];
	int history_count = 0;
	char line[LINE_SIZE];
	int last_status = 0;

	puts("Fruit Jam telnet shell");
	for (;;) {
		char *cmd;
		char *argv[16];
		int argc;

		prompt();
		if (!read_interactive_line(line, sizeof(line), history,
					   history_count))
			break;
		cmd = trim(line);
		if (!*cmd)
			continue;
		add_history(history, &history_count, cmd);

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
			puts("builtins: cd echo exit help history status");
			puts("simple commands are searched in /bin /usr/bin /sbin /usr/sbin");
			puts("line editing: up/down history, tab command/path completion");
			last_status = 0;
			continue;
		}
		if (!strcmp(argv[0], "history")) {
			int i;

			for (i = 0; i < history_count; i++)
				printf("%d %s\n", i + 1, history[i]);
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
