#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	printf("PROGRAM 1 IS RUNNING!\n");

	int i = 0;
	for (i = 0; i < 10; i++) {
		printf("1: %d\n", i);
		sleep(1);
	}

	return 0;
}
