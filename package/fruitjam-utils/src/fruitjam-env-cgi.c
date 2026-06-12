#include <stdio.h>
#include <stdlib.h>

static void print_env(const char *name)
{
	const char *value = getenv(name);
	printf("%s=%s\n", name, value ? value : "");
}

int main(void)
{
	puts("Content-Type: text/plain");
	puts("Access-Control-Allow-Origin: *");
	puts("Cache-Control: no-store");
	puts("Pragma: no-cache");
	putchar('\n');
	puts("Fruit Jam CGI OK");
	print_env("REQUEST_METHOD");
	print_env("QUERY_STRING");
	print_env("REMOTE_ADDR");
	print_env("REMOTE_PORT");
	print_env("SERVER_PROTOCOL");
	putchar('\n');
	print_env("PATH");
	return 0;
}
