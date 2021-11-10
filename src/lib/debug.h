#ifndef __LIB_DEBUG_H
#define __LIB_DEBUG_H

/* GCC lets us add "attributes" to functions, function
   parameters, etc. to indicate their properties.
   See the GCC manual for details. */
#define UNUSED __attribute__ ((unused))
#define NO_RETURN __attribute__ ((noreturn))
#define NO_INLINE __attribute__ ((noinline))
#define PRINTF_FORMAT(FMT, FIRST) __attribute__ ((format (printf, FMT, FIRST)))

/* Halts the OS, printing the source file name, line number, and
   function name, plus a user-specific message. */
#define PANIC(...) debug_panic (__FILE__, __LINE__, __func__, __VA_ARGS__)

void debug_thread (const void *, const char *file, int line, const char *func, const char *fmt, ...);
void debug_print (const char *file, int line, const char *func, const char *fmt, ...);
void debug_panic (const char *file, int line, const char *function,
                  const char *message, ...) PRINTF_FORMAT (4, 5) NO_RETURN;
void debug_backtrace (void);
void debug_backtrace_all (void);

#endif



/* This is outside the header guard so that debug.h may be
   included multiple times with different settings of NDEBUG. */
#undef ASSERT
#undef NOT_REACHED
#define NDEBUG

#ifndef NDEBUG
#define DEBUG_THREAD_T(t, fmt, ...) debug_thread (t, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define DEBUG_THREAD(fmt, ...) debug_thread (thread_current(), __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define DEBUG_PRINT(fmt, ...) debug_print (__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define ASSERT(CONDITION)                                       \
        if (CONDITION) { } else {                               \
                PANIC ("assertion `%s' failed.", #CONDITION);   \
        }
#define NOT_REACHED() PANIC ("executed an unreachable statement");
#else
#define DEBUG_THREAD_T(t, fmt, ...)
#define DEBUG_THREAD(fmt, ...)
#define DEBUG_PRINT(fmt, ...)
#define ASSERT(CONDITION) ((void) 0);
#define NOT_REACHED() for (;;)
#endif /* lib/debug.h */
