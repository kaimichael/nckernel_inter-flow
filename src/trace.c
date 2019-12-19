#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <nckernel/trace.h>

#include <fnmatch.h>

static void nck_trace_stderr(void *context, const char *file, int line, const char *func, const char *format, ...)
{
	static const char *file_pattern = NULL;
	static const char *func_pattern = NULL;
	const char *header;

	if (!file_pattern) {
		file_pattern = getenv("NCK_TRACE");
		if (!file_pattern) {
			// by default we trace no file
			// tracing needs to be explicitly turned on
			file_pattern = "";
		}
	}

	if (!func_pattern) {
		func_pattern = getenv("NCK_TRACE_FUNC");
		if (!func_pattern) {
			// by default we trace all functions
			func_pattern = "*";
		}
	}

	if (fnmatch(file_pattern, file, FNM_NOESCAPE)) {
		return;
	}

	if (fnmatch(func_pattern, func, FNM_NOESCAPE)) {
		return;
	}

	if (isatty(fileno(stderr))) {
		header = "\033[35m%s\033[0m:\033[32m%d\033[0m %s [\033[91m%p\033[0m] - ";
	} else {
		header = "%s:%d %s [%p] - ";
	}

	fprintf(stderr, header, file, line, func, context);

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	va_end(args);
}

void (*nck_trace_print)(void *context, const char *file, int line, const char *func, const char *format, ...) = nck_trace_stderr;
