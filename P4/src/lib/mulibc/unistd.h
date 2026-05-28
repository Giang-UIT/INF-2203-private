#ifndef UNISTD_H
#define UNISTD_H

#include <core/types.h>

_Noreturn void _exit(int status);
ssize_t        write(int fd, const void *src, size_t count);

ssize_t read(int fd, void *buf, size_t nbyte);

pid_t fork(void);
int   execv(const char *pathname, char *const argv[]);

int proc_spawn_munix(const char *pathname, char *const argv[]);

pid_t waitpid(pid_t pid, int *wstatus, int options);

#endif /* UNISTD_H */
