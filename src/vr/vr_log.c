#include <stdarg.h>
#include <stdio.h>

#include "vr_log.h"

static vr_log_sink_t sink = NULL;

void vr_log_set_sink(vr_log_sink_t s) {
	sink = s;
}

void vr_logf(const char *fmt, ...) {
	char line[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	if (sink) {
		sink(line);
		return;
	}
	printf("[vr] %s\n", line);
	fflush(stdout);
}
