#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <unistd.h>

/* === Utilities === */

/** Move cursor up via ANSI escape code */
static int fprintf_cursor_up(FILE *f, int lines)
{
    return fprintf(f, "\033[%dA", lines);
}

/** Move cursor down via ANSI escape code */
static int fprintf_cursor_down(FILE *f, int lines)
{
    return fprintf(f, "\033[%dB", lines);
}

/** Move cursor to a specific column via ANSI escape code */
static int fprintf_cursor_horiz(FILE *f, int c)
{
    return fprintf(f, "\033[%dG", c + 1);
}

static void delay_loop(size_t slowdown)
{
    unsigned long long delay_loops = 1ull << slowdown;
    for (unsigned long long i = 0; i < delay_loops; i++) {
        /* Do nothing, but trick the compiler into thinking that we are doing
         * something. If the compiler can tell that this loop is useless then
         * it will simply remove it during optimization. */
        asm volatile("");
    }
}

/* === Foot Race Setup === */

#define RACERS_MAX 8

static size_t slowdown  = 20;
static int    nthreads  = 8;  ///< Number of threads
static int    columns   = 50; ///< Columns to run
static int    laps      = 4;  ///< Laps to run
static int    variation = 3;  ///< Variation in racer speeds

static mtx_t print_mtx;

static brr_t lap_brr;

struct racer {
    int id; ///< Racer id
    int l;  ///< Lap count
    int c;  ///< Column position
    int f;  ///< Frame
};

#define LANE_OFFSET 1
#define LAP_DIGITS  1

static const char *RACER_ART[] = {
        ",O`",
        ".O.",
};

#define FRAME_CT (sizeof(RACER_ART) / sizeof(*RACER_ART))

/* === Racer Threads === */

#define RCR_DRAW  1
#define RCR_CLEAR 0

static int racer_print(struct racer *r, int mode)
{
    mtx_lock(&print_mtx);
    {
        const int lane_offset = 1;
        int       rows_up     = r->id + lane_offset;
        int       print_col   = r->c;

        /* Position cursor and print racer. */
        fprintf_cursor_up(stdout, rows_up);
        fprintf_cursor_horiz(stdout, print_col);
        switch (mode) {
        case RCR_DRAW: fprintf(stdout, " %s ", RACER_ART[r->f]); break;
        case RCR_CLEAR: fprintf(stdout, " %s ", "  "); break;
        }

        fprintf_cursor_horiz(stdout, columns + 3);
        fprintf(stdout, "| %*d", LAP_DIGITS, r->l);

        /* Reset cursor. */
        fprintf_cursor_down(stdout, rows_up);
        fprintf_cursor_horiz(stdout, 0);
    }
    mtx_unlock(&print_mtx);
    return 0;
}

static int racer_main(void *arg)
{
    struct racer *r = arg;

    for (r->l = 0; r->l < laps; r->l++) {
        racer_print(r, RCR_CLEAR);
        for (r->c = 0; r->c < columns; r->c++) {
            for (r->f = 0; r->f < (int) FRAME_CT; r->f++) {
                racer_print(r, RCR_DRAW);
                delay_loop(slowdown + r->id % variation);
            }
        }

        /* Wait for others */
        brr_wait(&lap_brr);
    }

    /* First one to finish stops the entire process.
     * This will look staggered unless the barrier is working properly. */
    _exit(0);
    return 0;
}

static void show_help(const char *progname)
{
    printf("%s - a barrier test\n"
           "    -n N    set number of racers (default %d)\n"
           "    -l L    set lap count (default %d)\n"
           "    -c C    set column count (default %d)\n"
           "    -s S    set slowdown (busy wait 2^S loops, default %zu)\n"
           "    -v V    vary racer slowdown by V (default %d)\n"
           "    -h      display help\n"
           "\nexample\n"
           "   %s -n 5\n",
           progname, nthreads, laps, columns, slowdown, variation, progname);
}

int main(int argc, char *argv[])
{
    int helpmode = 0;

    /* Process command line arguments. */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'n': nthreads = atoi(argv[i + 1]), i++; break;
            case 'l': laps = atoi(argv[i + 1]), i++; break;
            case 'c': columns = atoi(argv[i + 1]), i++; break;
            case 's': slowdown = atoi(argv[i + 1]), i++; break;
            case 'v': variation = atoi(argv[i + 1]), i++; break;
            case 'h':
            default: helpmode = 1;
            }
        } else helpmode = 1;
    }

    if (helpmode) {
        show_help(argv[0]);
        return 0;
    }

    /* Set up race */
    int          res;
    struct racer racers[nthreads];
    thrd_t       utids[nthreads];

    /* Avoid divide by zero. */
    if (variation <= 0) variation = 1;

    mtx_init(&print_mtx, mtx_plain);
    brr_init(&lap_brr, nthreads);

    for (int i = 0; i < nthreads; i++) {
        racers[i] = (struct racer){
                .id = i,
        };
        fprintf(stdout, "\n"); // Make room for ASCII art
    }

    fprintf(stdout, "\n"); // Make room for ASCII art

    /* Launch threads. */
    for (int i = 0; i < nthreads; i++) {
        res = thrd_create(&utids[i], racer_main, &racers[i]);
        if (res < 0) printf("[%d] thrd_create error: %s\n", i, strerror(-res));
    }

    /* Join threads. */
    for (int i = 0; i < nthreads; i++) {
#if __GLIBC__ && __STDC_HOSTED__
        /* glibc's thrd_join takes exit code parameter */
        int exit_code;
        res = thrd_join(utids[i], &exit_code);
#else
        /* mulibc's thrd_join returns the exit code. */
        int exit_code = res = thrd_join(utids[i]);
#endif
        if (res != thrd_success)
            printf("[%d] thrd_join error: %s\n", i, strerror(abs(res)));
        else if (exit_code < 0)
            printf("[%d] thread exited w/ err: %s\n", i, strerror(abs(res)));
    }

    /* Cleanup */
    brr_destroy(&lap_brr);
    mtx_destroy(&print_mtx);
    fprintf(stdout, "\n");

    return 0;
}

