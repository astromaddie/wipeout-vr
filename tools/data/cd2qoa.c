// Raw CD audio (s16le stereo 44.1 kHz) to QOA, skipping a pregap in bytes. Used by prepare_psx_data.sh
#define QOA_IMPLEMENTATION
#include "qoa.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
	if (argc != 4) { fprintf(stderr, "cd2qoa in.bin skip_bytes out.qoa\n"); return 2; }
	FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
	fseek(f, 0, SEEK_END); long n = ftell(f) - atol(argv[2]); fseek(f, atol(argv[2]), SEEK_SET);
	short *s = malloc(n); if (fread(s, 1, n, f) != (size_t)n) return 1;
	qoa_desc q = {.channels = 2, .samplerate = 44100, .samples = n / 4};
	return qoa_write(argv[3], s, &q) ? 0 : 1;
}
