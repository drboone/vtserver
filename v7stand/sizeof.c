#include <stdio.h>

int main(argc, argv)
	int argc;
	char *argv[];
{
	printf("short %d, int %d, long %d\n", sizeof(short),
		sizeof(int), sizeof(long));
	exit(0);
}
