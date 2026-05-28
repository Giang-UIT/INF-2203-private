#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <unistd.h>

/* === Circular Buffer === */

typedef int item_t;

struct circbuf {
    int    size; ///< Size of buffer
    int    head; ///< Index of front of queue (first item to take)
    int    tail; ///< Index of back of queue (next slot to push to)
    item_t buf[];
};

static int circ_init(struct circbuf *circ, int size)
{
    *circ = (struct circbuf){.size = size};
    return 0;
}

static int circ_len(struct circbuf *circ)
{
    return (circ->tail + circ->size - circ->head) % circ->size;
}

static int circ_spc(struct circbuf *circ)
{
    return (circ->head + circ->size - circ->tail - 1) % circ->size;
}

/** Push an item to the tail of a circular buffer */
static int circ_push(struct circbuf *circ, item_t item)
{
    if (circ_spc(circ) <= 0) return -ENOBUFS;
    circ->buf[circ->tail] = item;
    circ->tail            = (circ->tail + 1) % circ->size;
    return 0;
}

/** Take an item from the head of a circular buffer */
static item_t circ_shift(struct circbuf *circ)
{
    if (circ_len(circ) == 0) return -ENOENT;
    item_t item = circ->buf[circ->head];
    circ->head  = (circ->head + 1) % circ->size;
    return item;
}

#define circ_assert_len_spc(CIRC, LEN, SPC) \
    ({ \
        if (circ_len(CIRC) != LEN || circ_spc(CIRC) != SPC) { \
            fprintf(stderr, \
                    "%s: " \
                    "expected len=%d, spc=%d; " \
                    "got len=%d, spc=%d; " \
                    "(head=%d, tail=%d)\n", \
                    __func__, LEN, SPC, circ_len(circ), circ_spc(circ), \
                    circ->head, circ->tail); \
            return -EFAULT; \
        } \
    })

static int circ_test(struct circbuf *circ, int n)
{
    /* Repeat the test _n_ times */
    for (int i = 0; i < n; i++) {
        /* Add and remove _j_ items at a time, from 1 to buffer capacity */
        for (int j = 1; j < circ->size - 1; j++) {
            /* Add _j_ items */
            for (int k = 0; k < j; k++) circ_push(circ, k);

            /* Check len/spc calculations. */
            circ_assert_len_spc(circ, j, circ->size - 1 - j);

            /* Remove _j_ items */
            for (int k = 0; k < j; k++) {
                item_t item = circ_shift(circ);
                if (item != k) {
                    fprintf(stderr,
                            "%s: "
                            "expected item %d; "
                            "got out-of-sequence value %d\n",
                            __func__, k, item);
                    return -EILSEQ;
                }
            }

            /* Check len/spc calculations. */
            circ_assert_len_spc(circ, 0, circ->size - 1);
        }
    }

    fprintf(stderr, "%s: internal test ok\n", __func__);
    return 0;
}

/* === Message Passing Setup === */

static size_t buf_sz = 8;           ///< Size of buffer to use
static int    msg_ct = 1000 * 1000; ///< Count of messages to send per test
static int    n      = 4;           ///< Iterations of test to run
static int    v      = 0;           ///< Verbosity
static mtx_t  buf_mtx;
static cnd_t  buf_has_input, buf_has_space;

/* === Message Passing Threads === */

#define bail_if_error(res, fmt, ...) \
    ({ \
        if (res != thrd_success) \
            fprintf(stderr, "[%s] %s: " fmt, strerror(labs(res)), __func__, \
                    ##__VA_ARGS__); \
    })

#define reporterr(res, fmt, ...) \
    fprintf(stderr, "[%s] %s: " fmt, strerror(labs(res)), __func__, \
            ##__VA_ARGS__);

static int sender_main(void *arg)
{
    struct circbuf *circ = arg;
    int             res;

    /* Repeat test n times. */
    for (int i = 0; i < n; i++) {
        /* Send k messages. */
        for (int k = 0; k < msg_ct; k++) {
            res = mtx_lock(&buf_mtx);
            if (res != thrd_success) {
                reporterr(res, "could not acquire mutex\n");
            } else {
                /* Wait for space. */
                if (circ_spc(circ) == 0 && v)
                    fprintf(stderr, "sender blocked (buf full)\n");
                if (circ_spc(circ) == 0) {
                    res = cnd_wait(&buf_has_space, &buf_mtx);
                    if (res != thrd_success)
                        reporterr(res, "cnd_wait failure\n");
                }

                /* Send message. */
                circ_push(circ, k);

                /* Signal that input is ready. */
                cnd_signal(&buf_has_input);
            }
            mtx_unlock(&buf_mtx);
        }

        mtx_lock(&buf_mtx);
        fprintf(stdout, "[run #%d] sent %d messages\n", i, msg_ct);
        mtx_unlock(&buf_mtx);
    }
    return 0;
}

static int receiver_main(void *arg)
{
    struct circbuf *circ = arg;
    int             res;

    /* Repeat test n times. */
    for (int i = 0; i < n; i++) {
        /* Receive k messages. */
        for (int k = 0; k < msg_ct; k++) {
            res = mtx_lock(&buf_mtx);
            if (res != thrd_success) {
                reporterr(res, "could not acquire mutex\n");
            } else {
                /* Wait for input. */
                if (circ_len(circ) == 0 && v)
                    fprintf(stderr, "receiver blocked (buf full)\n");
                if (circ_len(circ) == 0) {
                    res = cnd_wait(&buf_has_input, &buf_mtx);
                    if (res != thrd_success)
                        reporterr(res, "cnd_wait failure\n");
                }

                /* Read message. */
                item_t item = circ_shift(circ);
                if (item < 0) {
                    reporterr(item, "error receiving message\n");
                    mtx_unlock(&buf_mtx);
                    return item;
                }

                /* Check sequence. */
                if (item != k) {
                    int res = -EILSEQ;
                    reporterr(res, "expected message %d; got %d\n", k, item);
                    mtx_unlock(&buf_mtx);
                    return res;
                }

                /* Signal that buffer has space. */
                cnd_signal(&buf_has_space);
            }
            mtx_unlock(&buf_mtx);
        }

        mtx_lock(&buf_mtx);
        fprintf(stdout, "[run #%d] rcvd %d messages; OK\n", i, msg_ct);
        mtx_unlock(&buf_mtx);
    }
    return 0;
}

/* === Message Passing Main === */

static void show_help(const char *progname)
{
    printf("%s - bounded buffer test\n"
           "    -b B    set buffer size (default %zu)\n"
           "    -c C    set message count (default %d)\n"
           "    -n N    repeat test N times (default %d)\n"
           "    -v      increase verbosity\n"
           "    -h      display help\n"
           "\nexample\n"
           "   %s -n 5\n",
           progname, buf_sz, msg_ct, n, progname);
}

int main(int argc, char *argv[])
{
    int helpmode = 0;

    /* Process command line arguments. */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'b': buf_sz = atoi(argv[i + 1]), i++; break;
            case 'c': msg_ct = atoi(argv[i + 1]), i++; break;
            case 'n': n = atoi(argv[i + 1]), i++; break;
            case 'v': v++; break;
            case 'h':
            default: helpmode = 1;
            }
        } else helpmode = 1;
    }

    if (helpmode) {
        show_help(argv[0]);
        return 0;
    }

    int res;

    /* Allocate circular buffer on stack. */
    size_t        alloc_sz = sizeof(struct circbuf) + sizeof(item_t) * buf_sz;
    unsigned char alloc_bytes[alloc_sz];
    struct circbuf *circ = (struct circbuf *) alloc_bytes;
    circ_init(circ, buf_sz);

    /* Do a sequential test of circular buffer first. */
    res = circ_test(circ, n);
    if (res < 0) return res;

    /* Set up threaded test. */
    mtx_init(&buf_mtx, mtx_plain);
    cnd_init(&buf_has_space);
    cnd_init(&buf_has_input);

    thrd_start_t thrd_fns[] = {sender_main, receiver_main};
    const size_t nthreads   = sizeof(thrd_fns) / sizeof(thrd_fns[0]);
    thrd_t       utids[nthreads];

    /* Launch threads. */
    for (size_t i = 0; i < nthreads; i++) {
        res = thrd_create(&utids[i], thrd_fns[i], circ);
        if (res < 0) {
            reporterr(res, "could not create thread %zu\n", i);
            return res;
        }
    }

    /* Join threads. */
    for (size_t i = 0; i < nthreads; i++) {
#if __GLIBC__ && __STDC_HOSTED__
        /* glibc's thrd_join takes exit code parameter */
        int exit_code;
        res = thrd_join(utids[i], &exit_code);
#else
        /* mulibc's thrd_join returns the exit code. */
        int exit_code = res = thrd_join(utids[i]);
#endif
        if (res != thrd_success)
            printf("[%zu] thrd_join error: %s\n", i, strerror(abs(res)));
        else if (exit_code < 0)
            printf("[%zu] thread exited w/ err: %s\n", i, strerror(abs(res)));
    }

    /* Cleanup. */
    cnd_destroy(&buf_has_input);
    cnd_destroy(&buf_has_space);
    mtx_destroy(&buf_mtx);
    return 0;
}

