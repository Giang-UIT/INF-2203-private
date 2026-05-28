#ifndef MUNIX_SYS_TYPES_H
#define MUNIX_SYS_TYPES_H

#if __linux__
/* Defer to Linux's version of this header. */
#include_next <sys/types.h>
#endif

#if __munix__

#include <stddef.h>

typedef long         ssize_t; ///< Signed size_t, for returning size or error
typedef long         off_t;   ///< File position offset
typedef long         loff_t;  ///< File position offset (explicitly 'long')
typedef unsigned     ino_t;   ///< Filesystem inode number
typedef int          pid_t;   ///< Process ID
typedef unsigned int dev_t;   ///< Device ID

#endif /* __munix__ */

#endif /* MUNIX_SYS_TYPES_H */
