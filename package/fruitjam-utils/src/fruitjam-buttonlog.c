// SPDX-License-Identifier: MIT
/*
 * Tiny fixed-schema SQLite file writer for Fruit Jam button events.
 *
 * Full SQLite is too large to exec reliably on this no-MMU RP2350 target once
 * services are running. This writer creates a valid SQLite 3 database with one
 * table and appends small records directly to that table's leaf b-tree page.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define LOG_FIFO "/run/fruitjam-buttonlog.fifo"

static volatile sig_atomic_t stop_requested;
static unsigned char page[PAGE_SIZE];
static unsigned char payload[768];
static unsigned char cell[1024];

static const char schema_sql[] =
	"CREATE TABLE button_events ("
	"id INTEGER PRIMARY KEY,"
	"ts INTEGER NOT NULL,"
	"name TEXT NOT NULL,"
	"gpio INTEGER NOT NULL,"
	"value INTEGER NOT NULL,"
	"action TEXT NOT NULL"
	")";

enum field_type {
	FIELD_NULL,
	FIELD_INT,
	FIELD_TEXT,
};

struct field {
	enum field_type type;
	long long integer;
	const char *text;
};

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-buttonlog {daemon DB|init DB|event DB NAME GPIO ACTION TS|dump DB [LIMIT]}\n");
}

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static void put_be16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v >> 8);
	p[1] = (unsigned char)v;
}

static void put_be32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void put_be64(unsigned char *p, long long v)
{
	unsigned long long u = (unsigned long long)v;
	int i;

	for (i = 7; i >= 0; i--) {
		p[i] = (unsigned char)u;
		u >>= 8;
	}
}

static unsigned int get_be16(const unsigned char *p)
{
	return ((unsigned int)p[0] << 8) | p[1];
}

static int varint_len(unsigned long long v)
{
	int len = 1;

	while (v > 0x7f && len < 9) {
		v >>= 7;
		len++;
	}
	return len;
}

static int put_varint(unsigned char *p, unsigned long long v)
{
	int len = varint_len(v);
	int i;

	for (i = len - 1; i >= 0; i--) {
		p[i] = (unsigned char)(v & 0x7f);
		if (i != len - 1)
			p[i] |= 0x80;
		v >>= 7;
	}
	return len;
}

static int get_varint(const unsigned char *p, unsigned long long *v)
{
	int i;

	*v = 0;
	for (i = 0; i < 9; i++) {
		*v = (*v << 7) | (p[i] & 0x7f);
		if (!(p[i] & 0x80))
			return i + 1;
	}
	return 9;
}

static int mkdir_p(const char *path)
{
	char tmp[128];
	char *p;

	if (strlen(path) >= sizeof(tmp)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(tmp, path);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

static void ensure_parent_dir(const char *path)
{
	char tmp[128];
	char *slash;

	snprintf(tmp, sizeof(tmp), "%s", path);
	slash = strrchr(tmp, '/');
	if (!slash || slash == tmp)
		return;
	*slash = '\0';
	mkdir_p(tmp);
}

static void log_path_for_db(const char *db_path, char *out, size_t out_len)
{
	char tmp[128];
	char *slash;

	snprintf(tmp, sizeof(tmp), "%s", db_path);
	slash = strrchr(tmp, '/');
	if (!slash || slash == tmp) {
		snprintf(out, out_len, "buttons.log");
		return;
	}
	*slash = '\0';
	snprintf(out, out_len, "%s/buttons.log", tmp);
}

static int read_full(int fd, void *buf, size_t len)
{
	char *p = buf;
	size_t done = 0;

	while (done < len) {
		ssize_t ret = read(fd, p + done, len - done);

		if (ret <= 0)
			return -1;
		done += (size_t)ret;
	}
	return 0;
}

static int write_full(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	size_t done = 0;

	while (done < len) {
		ssize_t ret = write(fd, p + done, len - done);

		if (ret <= 0)
			return -1;
		done += (size_t)ret;
	}
	return 0;
}

static int write_page(int fd, unsigned int page_no, const unsigned char *buf)
{
	if (lseek(fd, (off_t)(page_no - 1) * PAGE_SIZE, SEEK_SET) < 0)
		return -1;
	return write_full(fd, buf, PAGE_SIZE);
}

static int read_page(int fd, unsigned int page_no, unsigned char *buf)
{
	if (lseek(fd, (off_t)(page_no - 1) * PAGE_SIZE, SEEK_SET) < 0)
		return -1;
	return read_full(fd, buf, PAGE_SIZE);
}

static unsigned int serial_type(const struct field *field)
{
	if (field->type == FIELD_NULL)
		return 0;
	if (field->type == FIELD_INT)
		return 6;
	return 13 + (unsigned int)strlen(field->text) * 2;
}

static unsigned int field_data_len(const struct field *field)
{
	if (field->type == FIELD_NULL)
		return 0;
	if (field->type == FIELD_INT)
		return 8;
	return (unsigned int)strlen(field->text);
}

static int build_record(const struct field *fields, unsigned int count,
			unsigned char *out, size_t out_len)
{
	unsigned int serial_bytes = 0;
	unsigned int header_size;
	unsigned int i;
	size_t pos;

	for (i = 0; i < count; i++)
		serial_bytes += (unsigned int)varint_len(serial_type(&fields[i]));

	header_size = serial_bytes + 1;
	while ((unsigned int)varint_len(header_size) + serial_bytes != header_size)
		header_size = (unsigned int)varint_len(header_size) + serial_bytes;

	pos = (size_t)header_size;
	for (i = 0; i < count; i++)
		pos += field_data_len(&fields[i]);
	if (pos > out_len)
		return -1;

	pos = (size_t)put_varint(out, header_size);
	for (i = 0; i < count; i++)
		pos += (size_t)put_varint(out + pos, serial_type(&fields[i]));

	for (i = 0; i < count; i++) {
		if (fields[i].type == FIELD_INT) {
			put_be64(out + pos, fields[i].integer);
			pos += 8;
		} else if (fields[i].type == FIELD_TEXT) {
			size_t len = strlen(fields[i].text);

			memcpy(out + pos, fields[i].text, len);
			pos += len;
		}
	}
	return (int)pos;
}

static int build_cell(unsigned long long rowid, const struct field *fields,
		      unsigned int count, unsigned char *out, size_t out_len)
{
	int payload_len = build_record(fields, count, payload, sizeof(payload));
	size_t pos;

	if (payload_len < 0)
		return -1;

	pos = (size_t)put_varint(out, (unsigned int)payload_len);
	pos += (size_t)put_varint(out + pos, rowid);
	if (pos + (size_t)payload_len > out_len)
		return -1;
	memcpy(out + pos, payload, (size_t)payload_len);
	return (int)(pos + (size_t)payload_len);
}

static void init_btree_page(unsigned char *buf, unsigned int header_offset)
{
	memset(buf + header_offset, 0, PAGE_SIZE - header_offset);
	buf[header_offset] = 0x0d;
	put_be16(buf + header_offset + 1, 0);
	put_be16(buf + header_offset + 3, 0);
	put_be16(buf + header_offset + 5, PAGE_SIZE);
	buf[header_offset + 7] = 0;
}

static int add_cell_to_page(unsigned char *buf, unsigned int header_offset,
			    const unsigned char *cell_buf, unsigned int cell_len)
{
	unsigned int count = get_be16(buf + header_offset + 3);
	unsigned int content = get_be16(buf + header_offset + 5);
	unsigned int pointer_end = header_offset + 8 + count * 2 + 2;

	if (!content)
		content = PAGE_SIZE;
	if (cell_len > content || pointer_end > content - cell_len)
		return -1;

	content -= cell_len;
	memcpy(buf + content, cell_buf, cell_len);
	put_be16(buf + header_offset + 8 + count * 2, content);
	put_be16(buf + header_offset + 3, count + 1);
	put_be16(buf + header_offset + 5, content);
	return 0;
}

static int valid_db_header(int fd)
{
	char magic[16];
	struct stat st;

	if (fstat(fd, &st) < 0 || st.st_size < PAGE_SIZE * 2)
		return 0;
	if (lseek(fd, 0, SEEK_SET) < 0)
		return 0;
	if (read_full(fd, magic, sizeof(magic)) < 0)
		return 0;
	return !memcmp(magic, "SQLite format 3", 15);
}

static int create_db(int fd)
{
	struct field schema_fields[] = {
		{ FIELD_TEXT, 0, "table" },
		{ FIELD_TEXT, 0, "button_events" },
		{ FIELD_TEXT, 0, "button_events" },
		{ FIELD_INT, 2, NULL },
		{ FIELD_TEXT, 0, schema_sql },
	};
	int cell_len;

	if (ftruncate(fd, 0) < 0)
		return -1;

	memset(page, 0, sizeof(page));
	memcpy(page, "SQLite format 3", 15);
	page[15] = '\0';
	put_be16(page + 16, PAGE_SIZE);
	page[18] = 1;
	page[19] = 1;
	page[20] = 0;
	page[21] = 64;
	page[22] = 32;
	page[23] = 32;
	put_be32(page + 24, 1);
	put_be32(page + 28, 2);
	put_be32(page + 32, 0);
	put_be32(page + 36, 0);
	put_be32(page + 40, 1);
	put_be32(page + 44, 4);
	put_be32(page + 48, 0);
	put_be32(page + 52, 0);
	put_be32(page + 56, 1);
	put_be32(page + 60, 0);
	put_be32(page + 64, 0);
	put_be32(page + 68, 0);
	put_be32(page + 92, 1);
	put_be32(page + 96, 3048000);

	init_btree_page(page, 100);
	cell_len = build_cell(1, schema_fields, 5, cell, sizeof(cell));
	if (cell_len < 0 || add_cell_to_page(page, 100, cell, (unsigned int)cell_len) < 0)
		return -1;
	if (write_page(fd, 1, page) < 0)
		return -1;

	memset(page, 0, sizeof(page));
	init_btree_page(page, 0);
	return write_page(fd, 2, page);
}

static int open_db(const char *db_path)
{
	int fd;

	ensure_parent_dir(db_path);
	fd = open(db_path, O_RDWR | O_CREAT, 0644);
	if (fd < 0)
		return -1;
	if (!valid_db_header(fd) && create_db(fd) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static unsigned long long max_rowid_on_page(const unsigned char *buf)
{
	unsigned int count = get_be16(buf + 3);
	unsigned long long max_rowid = 0;
	unsigned int i;

	for (i = 0; i < count; i++) {
		unsigned int off = get_be16(buf + 8 + i * 2);
		unsigned long long ignored;
		unsigned long long rowid;
		int used;

		if (off >= PAGE_SIZE)
			continue;
		used = get_varint(buf + off, &ignored);
		get_varint(buf + off + used, &rowid);
		if (rowid > max_rowid)
			max_rowid = rowid;
	}
	return max_rowid;
}

static void append_text_log(const char *db_path, const char *name, int gpio,
			    const char *action, long long ts)
{
	char log_path[160];
	FILE *fp;

	log_path_for_db(db_path, log_path, sizeof(log_path));
	fp = fopen(log_path, "a");
	if (!fp)
		return;
	fprintf(fp, "%lld\t%s\t%d\t%s\n", ts, name, gpio, action);
	fclose(fp);
}

static int append_event(const char *db_path, const char *name, int gpio,
			const char *action, long long ts)
{
	struct field fields[] = {
		{ FIELD_NULL, 0, NULL },
		{ FIELD_INT, 0, NULL },
		{ FIELD_TEXT, 0, NULL },
		{ FIELD_INT, 0, NULL },
		{ FIELD_INT, 0, NULL },
		{ FIELD_TEXT, 0, NULL },
	};
	unsigned long long rowid;
	int fd;
	int cell_len;
	int ret = -1;

	fd = open_db(db_path);
	if (fd < 0)
		return -1;
	if (read_page(fd, 2, page) < 0)
		goto out;

	rowid = max_rowid_on_page(page) + 1;
	fields[1].integer = ts;
	fields[2].text = name;
	fields[3].integer = gpio;
	fields[4].integer = 0;
	fields[5].text = action;

	cell_len = build_cell(rowid, fields, 6, cell, sizeof(cell));
	if (cell_len < 0)
		goto out;
	if (add_cell_to_page(page, 0, cell, (unsigned int)cell_len) < 0) {
		fprintf(stderr, "fruitjam-buttonlog: database page full\n");
		goto out;
	}
	if (write_page(fd, 2, page) < 0)
		goto out;
	append_text_log(db_path, name, gpio, action, ts);
	ret = 0;

out:
	close(fd);
	return ret;
}

static int cmd_init(const char *db_path)
{
	int fd = open_db(db_path);

	if (fd < 0)
		return 1;
	close(fd);
	return 0;
}

static int cmd_event(const char *db_path, const char *name, const char *gpio_arg,
		     const char *action, const char *ts_arg)
{
	int gpio = (int)strtol(gpio_arg, NULL, 10);
	long long ts = strtoll(ts_arg, NULL, 10);

	return append_event(db_path, name, gpio, action, ts) < 0 ? 1 : 0;
}

static int cmd_dump(const char *db_path)
{
	char log_path[160];
	FILE *fp;
	char line[160];

	log_path_for_db(db_path, log_path, sizeof(log_path));
	fp = fopen(log_path, "r");
	if (!fp)
		return 1;
	while (fgets(line, sizeof(line), fp))
		fputs(line, stdout);
	fclose(fp);
	return 0;
}

static void handle_fifo_line(const char *db_path, char *line)
{
	char *cmd;
	char *name;
	char *gpio_arg;
	char *action;
	char *ts_arg;
	int gpio;
	long long ts;

	cmd = strtok(line, " \t\r\n");
	if (!cmd || strcmp(cmd, "event"))
		return;
	name = strtok(NULL, " \t\r\n");
	gpio_arg = strtok(NULL, " \t\r\n");
	action = strtok(NULL, " \t\r\n");
	ts_arg = strtok(NULL, " \t\r\n");
	if (!name || !gpio_arg || !action || !ts_arg)
		return;

	gpio = (int)strtol(gpio_arg, NULL, 10);
	ts = strtoll(ts_arg, NULL, 10);
	append_event(db_path, name, gpio, action, ts);
}

static void drain_fifo(const char *db_path, int fd)
{
	char buf[256];
	ssize_t len;
	char *line;
	char *next;

	for (;;) {
		len = read(fd, buf, sizeof(buf) - 1);
		if (len <= 0)
			return;
		buf[len] = '\0';

		line = buf;
		while (line && *line) {
			next = strchr(line, '\n');
			if (next)
				*next++ = '\0';
			handle_fifo_line(db_path, line);
			line = next;
		}
	}
}

static int cmd_daemon(const char *db_path)
{
	int read_fd;
	int keepalive_fd;

	if (cmd_init(db_path) != 0)
		return 1;

	unlink(LOG_FIFO);
	if (mkfifo(LOG_FIFO, 0600) < 0) {
		fprintf(stderr, "fruitjam-buttonlog: mkfifo %s: %s\n", LOG_FIFO, strerror(errno));
		return 1;
	}

	read_fd = open(LOG_FIFO, O_RDONLY | O_NONBLOCK);
	if (read_fd < 0) {
		fprintf(stderr, "fruitjam-buttonlog: open %s: %s\n", LOG_FIFO, strerror(errno));
		unlink(LOG_FIFO);
		return 1;
	}
	keepalive_fd = open(LOG_FIFO, O_WRONLY | O_NONBLOCK);

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	while (!stop_requested) {
		drain_fifo(db_path, read_fd);
		usleep(50000);
	}

	if (keepalive_fd >= 0)
		close(keepalive_fd);
	close(read_fd);
	unlink(LOG_FIFO);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "daemon"))
		return cmd_daemon(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "init"))
		return cmd_init(argv[2]);
	if (argc == 7 && !strcmp(argv[1], "event"))
		return cmd_event(argv[2], argv[3], argv[4], argv[5], argv[6]);
	if ((argc == 3 || argc == 4) && !strcmp(argv[1], "dump"))
		return cmd_dump(argv[2]);

	usage(stderr);
	return 2;
}
