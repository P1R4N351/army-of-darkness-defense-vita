/* Host replacement for FalsoJNI_Logger.c (which needs psp2 headers). */
#include <stdarg.h>
#include <stdio.h>
#define L(name, tag) void name(const char *fi, int li, const char *fn, const char *fmt, ...) { \
	va_list ap; va_start(ap, fmt); printf("[fjni " tag "] %s: ", fn); vprintf(fmt, ap); printf("\n"); va_end(ap); }
L(_fjni_log_info, "info")
L(_fjni_log_warn, "warn")
L(_fjni_log_debug, "debug")
L(_fjni_log_error, "error")
