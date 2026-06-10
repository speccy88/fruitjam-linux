#include "berry.h"
#include "be_repl.h"

#include <stdio.h>
#include <string.h>

#define BERRY_REPL_LINE_LEN 512

static int berry_result(bvm *vm, int res)
{
	switch (res) {
	case BE_OK:
		return 0;
	case BE_EXCEPTION:
		be_dumpexcept(vm);
		return 1;
	case BE_EXIT:
		return 0;
	case BE_MALLOC_FAIL:
		fputs("berry: memory allocation failed\n", stderr);
		return 1;
	case BE_IO_ERROR:
		fputs("berry: I/O error\n", stderr);
		return 1;
	default:
		return 1;
	}
}

static void usage(void)
{
	puts("Usage: berry [-e script] [script.be]");
	puts("       berry");
	puts("       berry -v");
}

static char *berry_readline(const char *prompt)
{
	static char line[BERRY_REPL_LINE_LEN];
	size_t len;

	fputs(prompt, stdout);
	fflush(stdout);

	if (!fgets(line, sizeof(line), stdin))
		return NULL;

	len = strlen(line);
	while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';

	return line;
}

int main(int argc, char **argv)
{
	bvm *vm;
	int res;

	if (argc == 2 && !strcmp(argv[1], "-h")) {
		usage();
		return 0;
	}

	if (argc == 2 && !strcmp(argv[1], "-v")) {
		puts("Berry " BERRY_VERSION);
		return 0;
	}

	vm = be_vm_new();
	if (!vm) {
		fputs("berry: failed to create VM\n", stderr);
		return 1;
	}

	be_module_path_set(vm, "/usr/lib/berry");
	be_module_path_set(vm, "/usr/share/berry");

	if (argc == 3 && !strcmp(argv[1], "-e"))
		res = be_loadstring(vm, argv[2]);
	else if (argc == 2)
		res = be_loadmode(vm, argv[1], 0);
	else if (argc == 1)
		res = be_repl(vm, berry_readline, NULL);
	else {
		usage();
		be_vm_delete(vm);
		return 1;
	}

	if (argc != 1 && res == BE_OK)
		res = be_pcall(vm, 0);

	res = berry_result(vm, res);
	be_vm_delete(vm);

	return res;
}
