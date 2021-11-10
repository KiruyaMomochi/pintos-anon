#ifndef __LIB_KERNEL_DEBUG_H
#define __LIB_KERNEL_DEBUG_H

void debug_thread (const void *, const char *file, int line, const char *func, const char *fmt, ...);
void debug_print (const char *file, int line, const char *func, const char *fmt, ...);

#ifdef DEBUG_KERNEL
#define DEBUG_THREAD_T(t, fmt, ...) debug_thread (t, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define DEBUG_THREAD(fmt, ...) debug_thread (thread_current(), __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define DEBUG_PRINT(fmt, ...) debug_print (__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define DEBUG_THREAD_T(t, fmt, ...)
#define DEBUG_THREAD(fmt, ...)
#define DEBUG_PRINT(fmt, ...)
#define ASSERT(CONDITION) ((void) 0);
#endif

#endif
