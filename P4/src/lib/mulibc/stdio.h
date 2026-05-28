#include <core/compiler.h>

#include <stdarg.h>
#include <stddef.h>

struct _file {
    int fd;
    int err;
    int eof;
};

typedef struct _file FILE;

extern FILE *stdin, *stdout, *stderr;

/**
 * @name Formatted I/O (printf)
 *
 * These are the classic `printf` functions, the C standard for formatted
 * output.
 *
   |       name |   v-variant | prints to                         |
   |-----------:|------------:|-----------------------------------|
   |   `printf` |   `vprintf` | @ref stdout                       |
   |  `fprintf` |  `vfprintf` | @ref FILE                         |
   |  `sprintf` |  `vsprintf` | string buffer (`char*`)           |
   | `snprintf` | `vsnprintf` | bounded string buffer (`char[n]`) |
 *
 * The v- variants take a va_list object instead of the '...' variable number
 * of parameters.
 *
 * In our implementation, all printf variants delegate to @ref vsnprintf.
 * That is where the actual format processing takes place.
 * Functions that write to a file call @ref vsnprintf first to format
 * to a string, then use the @ref write syscall to actually print
 * the formatted string.
 *
 * \dot "printf variant delegations"
    digraph {
        rankdir=LR
        node [shape=record, fontname=Helvetica, fontsize=10];
        edge [fontname=Helvetica, fontsize=10];
        { node[style=filled] vprintf vfprintf vsprintf vsnprintf }

        printf      [label="printf(fmt, ...)"]
        vprintf     [label="vprintf(fmt, va)"]
        fprintf     [label="fprintf(file, fmt, ...)"]
        vfprintf    [label="vfprintf(file, fmt, va)"]
        sprintf     [label="sprintf(buf, fmt, ...)"]
        vsprintf    [label="vsprintf(buf, fmt, va)"]
        snprintf    [label="snprintf(buf, n, fmt, ...)"]
        vsnprintf   [label="vsnprintf(buf, n, fmt, va)"]
        write       [label="write(fd, buf, n)"]

        { edge[label=""]
            printf -> vprintf
            fprintf -> vfprintf
            sprintf -> vsprintf
            snprintf -> vsnprintf
        }

        vsprintf -> vsnprintf [label="n=SIZE_MAX"];
        vprintf -> vfprintf [label="file=stdout"];
        vfprintf -> vsnprintf [label="1. format str"];
        vfprintf -> write [label="2. write str"];
    }
 * \enddot
 *
 * @see
 *
 * - C Standard printf: <https://en.cppreference.com/w/c/io/fprintf.html>
 * - C Standard vprintf: <https://en.cppreference.com/w/c/io/vfprintf>
 * - C Standard variable argument lists:
 *      <https://en.cppreference.com/w/c/variadic.html>
 */
///@{
ATTR_PRINTFLIKE(2, 3)
int sprintf(char *s, const char *format, ...);
int vsprintf(char *s, const char *format, va_list args);

ATTR_PRINTFLIKE(3, 4)
int snprintf(char *s, size_t n, const char *format, ...);
int vsnprintf(char *s, size_t n, const char *format, va_list args);

ATTR_PRINTFLIKE(2, 3)
int fprintf(FILE *f, const char *format, ...);
int vfprintf(FILE *f, const char *format, va_list args);

ATTR_PRINTFLIKE(1, 2)
int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
///@}

size_t fread(void *buf, size_t size, size_t count, FILE *stream);
char  *fgets(char *str, int count, FILE *stream);

static inline int  ferror(FILE *stream) { return stream->err; }
static inline int  feof(FILE *stream) { return stream->eof; }
static inline void clearerr(FILE *stream) { stream->err = stream->eof = 0; }

