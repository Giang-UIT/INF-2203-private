/**
 * @file
 * Implementation-defined constants
 *
 * In a freestanding environment (no OS), this header is provided by the
 * compiler, and it defines important constants like minimum and maximum
 * values for integer types (e.g. `INT_MIN` and `INT_MAX`.
 *
 * In a hosted environment, like Linux, the OS version of the header
 * also defines constants for the host environment (e.g. `PATH_MAX`).
 *
 * @see
 *
 * - See the C standard for more info about these essential constants
 *   that are supplied by the compiler:
 *   <https://en.cppreference.com/w/c/header/limits>
 *
 * - See the POSIX standard for more info about additinal constants
 *   that the OS defines.
 *   <https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/limits.h.html>
 */

/*
 * === Always include to the system's version of this header ===
 *
 * If compiling in a freestanding environment (Munix),
 * this gets the important integer min/max constants from the compiler.
 *
 * If compiling on a host OS (not Munix),
 * this gets the OS's constants like `PATH_MAX`.
 */
#include_next <limits.h>

/* === Definitions of OS constants for Munix === */

#if __munix__
#ifndef MUNIX_LIMITS_H
#define MUNIX_LIMITS_H

#define PATH_MAX 128

#endif /* MUNIX_LIMITS_H */
#endif /* __munix__ */
