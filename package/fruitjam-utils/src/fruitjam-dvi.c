// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam DVI renderer.
 *
 * /dev/fruitjam-dvi is a raw 640x480 RGB332 misc device. Keep this helper
 * line-buffered so it does not need a 300 KiB userspace framebuffer on no-MMU.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DVI_DEV "/dev/fruitjam-dvi"
#define WIDTH 640u
#define HEIGHT 480u
#define SCALE 2u
#define LEFT 16u
#define TOP 22u
#define CHAR_W (6u * SCALE)
#define CHAR_H (8u * SCALE)
#define MAX_LINES 28u
#define LINE_CHARS 84u
#define EXEC_TIMEOUT_MS 7000L

static char screen_lines[MAX_LINES][LINE_CHARS];
static unsigned int screen_line_count;

static const char *path_dirs[] = {
	"/bin", "/usr/bin", "/sbin", "/usr/sbin"
};

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-dvi {dashboard|text TEXT|stdin|tail FILE|exec COMMAND [ARGS...]|bars|pattern|show|start|forever|stop|clear|white|test|wili|wili-pattern|wili-show|wili-forever|wili-test}\n");
}

static unsigned char rgb332(unsigned char r, unsigned char g, unsigned char b)
{
	return ((r & 0xc0) >> 6) | ((g & 0xe0) >> 3) | (b & 0xe0);
}

static int write_all(int fd, const void *buf, size_t len)
{
	const unsigned char *p = buf;

	while (len) {
		ssize_t ret = write(fd, p, len);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!ret)
			return -1;
		p += ret;
		len -= (size_t)ret;
	}
	return 0;
}

static int dvi_command(const char *cmd)
{
	int fd = open(DVI_DEV, O_WRONLY);
	int ret;

	if (fd < 0) {
		fprintf(stderr, "fruitjam-dvi: open %s: %s\n", DVI_DEV,
			strerror(errno));
		return -1;
	}
	ret = write_all(fd, cmd, strlen(cmd));
	close(fd);
	return ret;
}

static int dvi_fill_and_show(const char *cmd)
{
	if (dvi_command(cmd) < 0)
		return 1;
	if (dvi_command("show") < 0)
		return 1;
	return 0;
}

static int dvi_fill_and_show_as(const char *fill_cmd, const char *show_cmd)
{
	if (dvi_command(fill_cmd) < 0)
		return 1;
	if (dvi_command(show_cmd) < 0)
		return 1;
	return 0;
}

static long now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static const unsigned char *glyph_rows(char c)
{
	static const unsigned char unknown[7] = { 0x1f, 0x11, 0x05, 0x02, 0x04, 0x00, 0x04 };
	static const unsigned char space[7] = { 0, 0, 0, 0, 0, 0, 0 };
	static const unsigned char colon[7] = { 0, 0x04, 0x04, 0, 0x04, 0x04, 0 };
	static const unsigned char dash[7] = { 0, 0, 0, 0x1f, 0, 0, 0 };
	static const unsigned char dot[7] = { 0, 0, 0, 0, 0, 0x0c, 0x0c };
	static const unsigned char slash[7] = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
	static const unsigned char eq[7] = { 0, 0, 0x1f, 0, 0x1f, 0, 0 };
	static const unsigned char plus[7] = { 0, 0x04, 0x04, 0x1f, 0x04, 0x04, 0 };
	static const unsigned char pct[7] = { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
	static const unsigned char nums[10][7] = {
		{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
		{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
		{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
		{ 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
		{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
		{ 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
		{ 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
		{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
		{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
		{ 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
	};
	static const unsigned char letters[26][7] = {
		{ 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
		{ 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
		{ 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
		{ 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
		{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
		{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
		{ 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
		{ 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
		{ 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
		{ 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
		{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
		{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
		{ 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
		{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
		{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
		{ 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
		{ 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
		{ 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
		{ 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
		{ 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
		{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
		{ 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
		{ 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
		{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
	};

	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z')
		return letters[c - 'A'];
	if (c >= '0' && c <= '9')
		return nums[c - '0'];
	switch (c) {
	case ' ': return space;
	case ':': return colon;
	case '-': return dash;
	case '.': return dot;
	case '/': return slash;
	case '=': return eq;
	case '+': return plus;
	case '%': return pct;
	default: return unknown;
	}
}

static void add_line(const char *fmt, ...)
{
	va_list ap;

	if (screen_line_count >= MAX_LINES)
		return;
	va_start(ap, fmt);
	vsnprintf(screen_lines[screen_line_count], LINE_CHARS, fmt, ap);
	va_end(ap);
	screen_line_count++;
}

static void add_wrapped(const char *text)
{
	char line[LINE_CHARS];
	size_t n = 0;

	while (*text && screen_line_count < MAX_LINES) {
		if (*text == '\r') {
			text++;
			continue;
		}
		if (*text == '\n') {
			line[n] = '\0';
			add_line("%s", line);
			n = 0;
			text++;
			continue;
		}
		if (n >= LINE_CHARS - 1) {
			line[n] = '\0';
			add_line("%s", line);
			n = 0;
		}
		line[n++] = *text++;
	}
	if (n && screen_line_count < MAX_LINES) {
		line[n] = '\0';
		add_line("%s", line);
	}
}

static void add_line_scrolling_text(const char *text)
{
	if (screen_line_count >= MAX_LINES) {
		memmove(screen_lines, screen_lines + 1,
			(MAX_LINES - 1) * sizeof(screen_lines[0]));
		screen_line_count = MAX_LINES - 1;
	}
	snprintf(screen_lines[screen_line_count], LINE_CHARS, "%s", text);
	screen_line_count++;
}

static void add_wrapped_scrolling(const char *text)
{
	char line[LINE_CHARS];
	size_t n = 0;

	while (*text) {
		if (*text == '\r') {
			text++;
			continue;
		}
		if (*text == '\n') {
			line[n] = '\0';
			add_line_scrolling_text(line);
			n = 0;
			text++;
			continue;
		}
		if (n >= LINE_CHARS - 1) {
			line[n] = '\0';
			add_line_scrolling_text(line);
			n = 0;
		}
		line[n++] = *text++;
	}
	if (n) {
		line[n] = '\0';
		add_line_scrolling_text(line);
	}
}

static int read_first_line(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t ret;

	if (!len)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		buf[0] = '\0';
		return -1;
	}
	ret = read(fd, buf, len - 1);
	close(fd);
	if (ret < 0) {
		buf[0] = '\0';
		return -1;
	}
	buf[ret] = '\0';
	while (ret > 0 && (buf[ret - 1] == '\n' || buf[ret - 1] == '\r' ||
			   buf[ret - 1] == ' ' || buf[ret - 1] == '\t'))
		buf[--ret] = '\0';
	return 0;
}

static int gpio_value(unsigned int gpio)
{
	char path[64];
	char value[8];

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	if (read_first_line(path, value, sizeof(value)) < 0)
		return -1;
	return value[0] == '0' ? 0 : 1;
}

static const char *usb_state(int power, int dp, int dm)
{
	if (power < 0)
		return "unknown";
	if (power == 0)
		return "power-off";
	if (dp < 0 || dm < 0)
		return "unknown";
	if (!dp && !dm)
		return "no-device-or-reset";
	if (dp && !dm)
		return "full-speed-device";
	if (!dp && dm)
		return "low-speed-device";
	return "invalid-both-lines-high";
}

static void make_dashboard(void)
{
	char line[96];
	char uptime[48] = "?";
	char memfree[48] = "?";
	int usbp = gpio_value(11);
	int dp = gpio_value(1);
	int dm = gpio_value(2);

	screen_line_count = 0;
	read_first_line("/proc/uptime", uptime, sizeof(uptime));
	read_first_line("/proc/meminfo", memfree, sizeof(memfree));

	add_line("Fruit Jam Linux");
	add_line("================");
	add_line("Uptime: %s", uptime);
	add_line("Memory: %s", memfree);
	add_line("");
	add_line("Devices");
	add_line("  DVI:       %s", access(DVI_DEV, W_OK) == 0 ? "ready" : "missing");
	add_line("  Audio:     %s", access("/dev/fruitjam-audio", W_OK) == 0 ? "ready" : "missing");
	add_line("  I2C0:      %s", access("/dev/i2c-0", R_OK | W_OK) == 0 ? "ready" : "missing");
	add_line("  NeoPixels: %s", access("/dev/neopixels", W_OK) == 0 ? "ready" : "missing");
	add_line("  SD card:   %s", access("/mnt/sd", R_OK | W_OK) == 0 ? "mounted" : "not mounted");
	snprintf(line, sizeof(line), "  USB host:  %s p=%d dp=%d dm=%d",
		 usb_state(usbp, dp, dm), usbp, dp, dm);
	add_line("%s", line);
	add_line("");
	add_line("Quick commands");
	add_line("  fruitjam-rtttl scale");
	add_line("  fruitjam-dvi text Hello from Linux");
	add_line("  berry-run /root/berry/run-all.be");
	add_line("  sh /root/sh/run-all.sh");
}

static void draw_text_on_line(unsigned char *line, unsigned int y)
{
	unsigned int text_line;

	for (text_line = 0; text_line < screen_line_count; text_line++) {
		unsigned int y0 = TOP + text_line * CHAR_H;
		unsigned int glyph_row;
		size_t i;

		if (y < y0 || y >= y0 + 7u * SCALE)
			continue;
		glyph_row = (y - y0) / SCALE;
		for (i = 0; screen_lines[text_line][i]; i++) {
			const unsigned char *rows = glyph_rows(screen_lines[text_line][i]);
			unsigned char bits = rows[glyph_row];
			unsigned int col;

			for (col = 0; col < 5; col++) {
				if (bits & (1u << (4 - col))) {
					unsigned int x0 = LEFT + (unsigned int)i * CHAR_W + col * SCALE;
					unsigned int sx;

					for (sx = 0; sx < SCALE && x0 + sx < WIDTH; sx++)
						line[x0 + sx] = 0xff;
				}
			}
		}
	}
}

static int render_screen(void)
{
	unsigned char line[WIDTH];
	unsigned int y;
	int fd;
	unsigned char bg = rgb332(0, 24, 40);
	unsigned char bar = rgb332(0, 80, 120);

	fd = open(DVI_DEV, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "fruitjam-dvi: open %s: %s\n", DVI_DEV,
			strerror(errno));
		return 1;
	}

	for (y = 0; y < HEIGHT; y++) {
		memset(line, y < 12 ? bar : bg, sizeof(line));
		if (y >= HEIGHT - 10)
			memset(line, rgb332(80, 40, 0), sizeof(line));
		draw_text_on_line(line, y);
		if (write_all(fd, line, sizeof(line)) < 0) {
			fprintf(stderr, "fruitjam-dvi: write frame: %s\n",
				strerror(errno));
			close(fd);
			return 1;
		}
	}
	close(fd);
	if (dvi_command("show") < 0)
		return 1;
	return 0;
}

static void add_argv_text(int argc, char **argv, int start)
{
	int i;
	char joined[512];
	size_t used = 0;

	screen_line_count = 0;
	for (i = start; i < argc; i++) {
		int ret = snprintf(joined + used, sizeof(joined) - used,
				   "%s%s", i > start ? " " : "", argv[i]);

		if (ret < 0)
			break;
		if ((size_t)ret >= sizeof(joined) - used) {
			used = sizeof(joined) - 1;
			break;
		}
		used += (size_t)ret;
	}
	joined[used] = '\0';
	add_wrapped(joined);
}

static int read_stdin_text(void)
{
	char buf[128];

	screen_line_count = 0;
	while (fgets(buf, sizeof(buf), stdin) && screen_line_count < MAX_LINES)
		add_wrapped(buf);
	return render_screen();
}

static int tail_file(const char *path)
{
	FILE *fp;
	char buf[LINE_CHARS];

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "fruitjam-dvi: open %s: %s\n", path, strerror(errno));
		return 1;
	}
	screen_line_count = 0;
	while (fgets(buf, sizeof(buf), fp)) {
		if (screen_line_count >= MAX_LINES) {
			memmove(screen_lines, screen_lines + 1,
				(MAX_LINES - 1) * sizeof(screen_lines[0]));
			screen_line_count = MAX_LINES - 1;
		}
		add_wrapped(buf);
	}
	fclose(fp);
	return render_screen();
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

	for (i = 0; i < sizeof(path_dirs) / sizeof(path_dirs[0]); i++) {
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

static int render_exec(char **argv)
{
	char buf[128];
	int pipefd[2];
	pid_t pid;
	int status = 127;
	int render_ret;
	volatile int child_done = 0;
	volatile int pipe_done = 0;
	volatile int timed_out = 0;
	long deadline;

	if (pipe(pipefd) < 0) {
		fprintf(stderr, "fruitjam-dvi: pipe: %s\n", strerror(errno));
		return 1;
	}

	pid = vfork();
	if (pid < 0) {
		fprintf(stderr, "fruitjam-dvi: vfork: %s\n", strerror(errno));
		close(pipefd[0]);
		close(pipefd[1]);
		return 1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		if (pipefd[1] > STDERR_FILENO)
			close(pipefd[1]);
		exec_child(&argv[2]);
	}

	close(pipefd[1]);
	screen_line_count = 0;
	add_line("Fruit Jam command");
	add_line("=================");
	add_wrapped_scrolling(argv[2]);
	add_line("");

	if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) < 0)
		add_wrapped_scrolling("pipe nonblock failed");

	deadline = now_ms() + EXEC_TIMEOUT_MS;
	while (!child_done || !pipe_done) {
		long remaining;

		for (;;) {
			ssize_t got = read(pipefd[0], buf, sizeof(buf) - 1);

			if (got > 0) {
				buf[got] = '\0';
				add_wrapped_scrolling(buf);
				continue;
			}
			if (!got) {
				pipe_done = 1;
				break;
			}
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			snprintf(buf, sizeof(buf), "pipe read: %s", strerror(errno));
			add_wrapped_scrolling(buf);
			pipe_done = 1;
			break;
		}

		if (!child_done) {
			pid_t waited = waitpid(pid, &status, WNOHANG);

			if (waited == pid)
				child_done = 1;
			else if (waited < 0 && errno != EINTR) {
				status = 127 << 8;
				child_done = 1;
			}
		}
		if (child_done && pipe_done)
			break;

		remaining = deadline - now_ms();
		if (remaining <= 0) {
			timed_out = 1;
			kill(pid, SIGKILL);
			break;
		}
		if (!pipe_done) {
			fd_set rfds;
			struct timeval tv;

			FD_ZERO(&rfds);
			FD_SET(pipefd[0], &rfds);
			tv.tv_sec = remaining > 200 ? 0 : remaining / 1000;
			tv.tv_usec = (remaining > 200 ? 200 : remaining % 1000) * 1000;
			if (select(pipefd[0] + 1, &rfds, NULL, NULL, &tv) < 0 &&
			    errno != EINTR)
				pipe_done = 1;
		} else {
			usleep(20000);
		}
	}

	close(pipefd[0]);
	while (!child_done && waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			status = 127 << 8;
			break;
		}
	}
	if (timed_out)
		add_wrapped_scrolling("timeout");
	if (WIFEXITED(status)) {
		snprintf(buf, sizeof(buf), "exit %d", WEXITSTATUS(status));
		add_wrapped_scrolling(buf);
	} else if (WIFSIGNALED(status)) {
		snprintf(buf, sizeof(buf), "signal %d", WTERMSIG(status));
		add_wrapped_scrolling(buf);
	}

	render_ret = render_screen();
	if (render_ret)
		return render_ret;
	if (timed_out)
		return 124;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 1;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "dashboard";

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "bars") || !strcmp(cmd, "pattern") ||
	    !strcmp(cmd, "clear") || !strcmp(cmd, "black") ||
	    !strcmp(cmd, "white"))
		return dvi_fill_and_show(cmd);
	if (!strcmp(cmd, "wili") || !strcmp(cmd, "wili-pattern"))
		return dvi_fill_and_show_as("wili-pattern", "wili-show");
	if (!strcmp(cmd, "start") || !strcmp(cmd, "show") ||
	    !strcmp(cmd, "on") || !strcmp(cmd, "stop") ||
	    !strcmp(cmd, "off") || !strcmp(cmd, "forever") ||
	    !strcmp(cmd, "continuous") || !strcmp(cmd, "test") ||
	    !strcmp(cmd, "wili-show") || !strcmp(cmd, "wili-frame") ||
	    !strcmp(cmd, "wili-forever") || !strcmp(cmd, "wili-continuous") ||
	    !strcmp(cmd, "wili-test"))
		return dvi_command(cmd) < 0 ? 1 : 0;
	if (!strcmp(cmd, "dashboard")) {
		make_dashboard();
		return render_screen();
	}
	if (!strcmp(cmd, "text")) {
		if (argc < 3) {
			usage(stderr);
			return 1;
		}
		add_argv_text(argc, argv, 2);
		return render_screen();
	}
	if (!strcmp(cmd, "stdin"))
		return read_stdin_text();
	if (!strcmp(cmd, "tail")) {
		if (argc != 3) {
			usage(stderr);
			return 1;
		}
		return tail_file(argv[2]);
	}
	if (!strcmp(cmd, "exec")) {
		if (argc < 3) {
			usage(stderr);
			return 1;
		}
		return render_exec(argv);
	}

	usage(stderr);
	return 1;
}
