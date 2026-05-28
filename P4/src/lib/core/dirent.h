/**
 * @file
 * Directory entries
 *
 * @see
 *
 * - `man 3 readder`
 * - POSIX standard:
 *   <https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/dirent.h.html>
 */

/* === If not Munix, defer to host OS's version of this header === */

#if !__munix__
#include_next <dirent.h>
#endif

/* === Definitions for Munix= === */

#if __munix__
#ifndef MUNIX_DIRENT_H
#define MUNIX_DIRENT_H

#include <sys/types.h>

enum dirtype {
    DT_UNKNOWN = 0, ///< Unknown file type
    DT_CHR,         ///< Character device
    DT_DIR,         ///< Directory
    DT_REG,         ///< Regular file
};

/**
 * Entry in a directory listing
 *
 * Use for reading directories with `readdir`.
 * See `man 2 readdir` on Linux for how this structure is used on Linux.
 */
struct dirent {
    ino_t          d_ino;       ///< Inode number
    off_t          d_off;       ///< Not an offset
    unsigned short d_reclen;    ///< Record length
    unsigned char  d_type;      ///< Type of file
    char           d_name[256]; ///< Filename
};

#endif /* MUNIX_DIRENT_H */
#endif /* __munix__ */
