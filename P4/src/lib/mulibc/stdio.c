#include "stdio.h"

#include "errno.h"

#include "unistd.h"

#include <stdarg.h>

/* === Instances of file objects and standard pointers === */

static FILE files[] = {
        [0] = {.fd = 0},
        [1] = {.fd = 1},
        [2] = {.fd = 2},
};

FILE *stdin  = &files[0];
FILE *stdout = &files[1];
FILE *stderr = &files[2];

/* === Formatted output (printf) === */

/**
 * Print to file, with args from va_list
 *
 * \callgraph
 */
int vfprintf(FILE *f, const char *fmt, va_list va)
{
    int bufsz = 256;
try_format : {
    char buf[bufsz];

    /* Format message. */
    va_list va_tmp;
    va_copy(va_tmp, va);
    int res = vsnprintf(buf, bufsz, fmt, va);
    va_end(va_tmp);
    if (res < 0) return res;

    /* If the formatted output was too big for the buffer,
     * try again with a buffer that is the correct size. */
    if (res >= bufsz) {
        bufsz = res + 1;
        goto try_format;
    }

    /* If formatting was successful, write to the file descriptor. */
    res = write(f->fd, buf, res);
    return res;
}
}

/**
 * Print to file
 *
 * \callgraph
 */
ATTR_PRINTFLIKE(2, 3)
int fprintf(FILE *f, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int res = vfprintf(f, format, args);
    va_end(args);
    return res;
}

/**
 * Print to stdout, with args from va_list
 *
 * \callgraph
 */
int vprintf(const char *format, va_list args)
{
    return vfprintf(stdout, format, args);
}

/**
 * Print to stdout
 *
 * \callgraph
 */
ATTR_PRINTFLIKE(1, 2)
int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int res = vfprintf(stdout, format, args);
    va_end(args);
    return res;
}

/* === File input === */

/**
 * Read bytes from a file
 *
 * Reads 'count' objects, each of 'size' bytes, from an input file.
 *
 * \note
 *  This implementation is much simpler than the standard.
 *  Here, we only make one 'read' syscall. The standard
 *  says that 'fread' should call 'fgetc' (read a single character)
 *  for every byte read. To do that properly, we would a buffer in
 *  the FILE object, and we would need recursive mutexes to guard
 *  the buffer. By doing a single read, we defer to the kernel to
 *  make the 'read' call atomic from the perspective of the process.
 *
 * See:
 *  - `man 3 fread`
 *  - C standard: <https://en.cppreference.com/w/c/io/fread.html>
*/
size_t fread(void *buf, size_t size, size_t count, FILE *stream)
{
    if (size == 0 || count == 0) return 0;

    size_t  read_size = size * count;
    ssize_t res       = read(stream->fd, buf, read_size);

    /* Check for error/eof. */
    if (res < 0) stream->err = 1;
    else if (res == 0) stream->eof = 1;

    /* Return number of objects read successfully. */
    return res / size;
}

/**
 * Read a string from a file
 *
 * See:
 *  - `man 3 fgets`
 *  - C standard: <https://en.cppreference.com/w/c/io/fgets.html>
 */
char *fgets(char *str, int count, FILE *stream)
{
    for (;;) {
        ssize_t res = read(stream->fd, str, count - 1);

        /* Check for error/eof. */
        if (res == -EAGAIN) { /* Non-blocking I/O */
            continue;

        } else if (res < 0) { /* Other error */
            stream->err = 1;
            return NULL;

        } else if (res == 0) { /* End of file */
            stream->eof = 1;
            return NULL;
        }

        str[res] = '\0'; // Write null terminator.
        return str;      // Return str to indicate success.
    }
}

