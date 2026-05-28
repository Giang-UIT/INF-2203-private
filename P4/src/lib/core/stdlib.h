#ifndef STDLIB_H
#define STDLIB_H

#include <stdint.h>

int atoi(const char *a);

/* Integer absolute value functions */

static inline int abs(int n) { return n < 0 ? -n : n; }
static inline int labs(long n) { return n < 0 ? -n : n; }
static inline int llabs(long long n) { return n < 0 ? -n : n; }
static inline int imaxabs(intmax_t n) { return n < 0 ? -n : n; }

#endif /* STDLIB_H */
