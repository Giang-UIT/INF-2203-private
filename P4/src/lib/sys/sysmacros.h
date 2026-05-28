#ifndef MUNIX_SYSMACROS_H
#define MUNIX_SYSMACROS_H

#if __linux__
/* Defer to Linux's version of this header. */
#include_next <sys/sysmacros.h>
#endif

#if __munix__

#define MAKEDEV(MAJ, MIN) ((MAJ << 8) | (MIN))
#define MAJOR(DEV)        (DEV >> 8)
#define MINOR(DEV)        (DEV & 0xff)

#endif /* __munix__ */

#endif /* MUNIX_SYSMACROS_H */
