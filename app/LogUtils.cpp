#include "LogUtils.h"
#include <stdio.h>
#include <string.h>
#define SAFE(c) ( (c >= ' ' && c <= '~') ? c : '.' )
void hex_dump(unsigned char *buf, long size) {
	long i;     // current buffer index
	long n;     // bytes left in buffer
	int j;


	static int WIDE = 16;
	static int FULL = 0;
	static time_t Mtime;
	for (i = 0; i < size; i += WIDE) {
		n = size - i;

		// (1) Display address on left.

		if (FULL) {
			printf("%.8lX:", i);        // display all lines in "full" mode

		} else {
			unsigned char *pl = &buf[i - WIDE];  // previous line
			unsigned char *cl = &buf[i];         // current line
			unsigned char *nl = &buf[i + WIDE];  // next line

			// If current line is the same as previous line then skip it.
			if (n >= WIDE && i >= WIDE && memcmp(cl, pl, WIDE) == 0) {
				continue;
			}

			// If current line is the same as next line then mark it.
			if (n >= 2 * WIDE && memcmp(cl, nl, WIDE) == 0) {
				printf("%.8lX*", i);

				// Otherwise display current line as usual.
			} else {
				printf("%.8lX:", i);
			}
		}

		// (2) Display bytes in hexadecimal format.

		for (j = 0; j < WIDE; j += 1) {
			if (j < n) {
				printf(" %.2X", buf[i + j]);
			} else {
				printf(" --");   // file not mod-16; partial last line
			}
		}

		// (3) Display bytes as ascii characters on right.

		printf("  ");
		for (j = 0; j < WIDE; j += 1) {
			if (j < n) {
				printf("%c", SAFE(buf[i + j]));
			}
		}
		printf("\n");
	}
	printf("%.8lX: end (%ld bytes) mtime %ld\n", size, size, Mtime);
}

