// SPDX-License-Identifier: MIT
/*
 * Tiny JSON wrapper for running whitelisted Berry examples from HTTP.
 *
 * The main Fruit Jam CGI is large enough that spawning Berry underneath it can
 * fail on no-MMU systems when Berry needs a contiguous flat-binary allocation.
 * This helper is exec'd by the CGI for action=berry-run, replacing the larger
 * process before Berry itself is launched.
 */

#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BERRY_BIN "/usr/bin/berry"
#define BERRY_DIR "/root/berry"
#define BERRY_SCRIPT_MAX 63

static const char *const berry_scripts[] = {
	"00-hello.be",
	"01-language-tour.be",
	"02-files-and-sd.be",
	"03-buttons.be",
	"04-adc-summary.be",
	"05-usbhost-status.be",
	"neopixels.be",
	"neopixel-colors.be",
	"neopixel-rainbow-10s.be",
	"run-all.be",
	"run-visual.be",
};

static void json_string(const char *s)
{
	putchar('"');
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;

		switch (c) {
		case '\\':
			fputs("\\\\", stdout);
			break;
		case '"':
			fputs("\\\"", stdout);
			break;
		case '\n':
			fputs("\\n", stdout);
			break;
		case '\r':
			fputs("\\r", stdout);
			break;
		case '\t':
			fputs("\\t", stdout);
			break;
		default:
			if (c < 0x20)
				printf("\\u%04x", c);
			else
				putchar(c);
			break;
		}
	}
	putchar('"');
}

static void json_error(const char *message)
{
	printf("{\"ok\":false,\"error\":");
	json_string(message);
	puts("}");
}

static void drop_page_cache(void)
{
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);

	if (fd < 0)
		return;
	write(fd, "3\n", 2);
	close(fd);
}

static int valid_berry_script(const char *name)
{
	size_t i;
	size_t len;

	if (!name || !*name || name[0] == '.')
		return 0;
	len = strlen(name);
	if (len > BERRY_SCRIPT_MAX || len < 4)
		return 0;
	if (strcmp(name + len - 3, ".be"))
		return 0;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];

		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			return 0;
	}
	return 1;
}

static int known_berry_script(const char *name)
{
	size_t i;

	if (!valid_berry_script(name))
		return 0;
	for (i = 0; i < ARRAY_SIZE(berry_scripts); i++) {
		if (!strcmp(name, berry_scripts[i]))
			return 1;
	}
	return 0;
}

static int berry_script_path(const char *name, char *path, size_t path_len)
{
	int ret;

	if (!known_berry_script(name))
		return -1;
	ret = snprintf(path, path_len, "%s/%s", BERRY_DIR, name);
	return ret > 0 && (size_t)ret < path_len ? 0 : -1;
}

static int run_capture_timeout(char *const argv[], char *out,
			       size_t out_len, unsigned int timeout_ms)
{
	char path[64];
	int fd;
	pid_t pid;
	int status = 127;
	volatile unsigned int waited_ms = 0;
	ssize_t ret;

	if (out_len)
		out[0] = '\0';
	snprintf(path, sizeof(path), "/tmp/fj-berry-json-%ld.out", (long)getpid());
	fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return 127;
	pid = vfork();
	if (pid < 0) {
		close(fd);
		unlink(path);
		return 127;
	}
	if (pid == 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
		execv(argv[0], argv);
		_exit(127);
	}

	for (;;) {
		pid_t done = waitpid(pid, &status, WNOHANG);

		if (done == pid)
			break;
		if (done < 0) {
			status = 127;
			break;
		}
		if (timeout_ms && waited_ms >= timeout_ms) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			status = 124 << 8;
			break;
		}
		usleep(20000);
		waited_ms += 20;
	}

	lseek(fd, 0, SEEK_SET);
	if (out_len) {
		ret = read(fd, out, out_len - 1);
		if (ret < 0)
			out[0] = '\0';
		else
			out[ret] = '\0';
	}
	close(fd);
	unlink(path);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 127;
}

int main(int argc, char **argv)
{
	const char *script;
	char path[sizeof(BERRY_DIR) + 1 + BERRY_SCRIPT_MAX];
	char output[768];
	int ret;

	if (argc != 2) {
		json_error("usage: fruitjam-berry-json SCRIPT.be");
		return 0;
	}
	script = argv[1];
	if (berry_script_path(script, path, sizeof(path)) < 0) {
		json_error("bad berry script");
		return 0;
	}
	if (access(path, R_OK) < 0) {
		json_error("berry script not found");
		return 0;
	}
	drop_page_cache();
	{
		char *const run_argv[] = { BERRY_BIN, path, NULL };

		ret = run_capture_timeout(run_argv, output, sizeof(output), 22000);
	}
	printf("{\"ok\":%s,\"exit\":%d,\"source\":\"tiny-berry-json\",\"script\":",
	       ret == 0 ? "true" : "false", ret);
	json_string(script);
	printf(",\"output\":");
	json_string(output);
	puts("}");
	return 0;
}
