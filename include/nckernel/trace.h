#ifndef _NCK_TRACE_H_
#define _NCK_TRACE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* With this function pointer `nck_trace_print` a program can customize the
 * trace output.  The default implementation writes to stdout and uses the
 * environment variables NCK_TRACE and NCK_TRACE_FUNC.
 *
 * NCK_TRACE must be set to a glob pattern to filter the files which write to
 * the trace log. If the variable is not set no output is written. To show all
 * trace output set "NCK_TRACE=*".
 *
 * NCK_TRACE_FUNC can be used to filter specific function names. It also
 * accepts glob patterns. By default all functions will be traced.
 */

extern void (*nck_trace_print)(void *context, const char *file, int line,
		const char *func, const char *format, ...)
	__attribute__ ((format (printf, 5, 6)));

// __FILENAME__ can be defined by the programmer or by the build system to make the
// trace output more legible
#ifndef __FILENAME__
#define __FILENAME__ __FILE__
#endif

/* nck_trace - this should be the preferred way to write to the trace log. It
 * automatically sets the correct file, line and func parameter so that only a
 * context and the message must be provided.
 *
 * The second parameter must be a format string as accepted by printf, followed
 * by the the arguments.
 */
#ifdef NDEBUG
# define nck_trace(context, ...) ((void)0)
#else
# define nck_trace(context, ...) nck_trace_print((context), __FILENAME__, __LINE__, __func__, __VA_ARGS__)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_TRACE_H_ */
