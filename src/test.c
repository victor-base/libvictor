#include <victor/victor.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	printf("%s(%d) - %s\n", argv[0], argc, __LIB_VERSION());
}