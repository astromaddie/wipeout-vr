#ifndef VR_LOG_H
#define VR_LOG_H

// "[vr] ..." state-change log. The line is printed without the prefix being
// part of the format; tests can capture lines with a sink.

#if defined(__GNUC__)
	#define VR_PRINTF_LIKE(a, b) __attribute__((format(printf, a, b)))
#else
	#define VR_PRINTF_LIKE(a, b)
#endif

typedef void (*vr_log_sink_t)(const char *line);

void vr_logf(const char *fmt, ...) VR_PRINTF_LIKE(1, 2);
void vr_log_set_sink(vr_log_sink_t sink);

#endif
