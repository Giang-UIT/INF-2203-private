#include "unistd.h"

#include <errno.h>

#include <sys/syscall.h>

#include <stddef.h>

_Noreturn void _exit(int status)
{
#if __linux__
    syscall(__NR_exit_group, status);
#elif __munix__
    syscall(SYS_exit, status);
#endif

    /* If the syscall fails, attempt to cause an exception. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    static int bad_val = 0;
    bad_val            = 10 / 0;
#pragma GCC diagnostic pop

    /* If the exception fails, go into an infinite loop to avoid returning. */
    for (;;)
        ;
}

ssize_t write(int fd, const void *src, size_t count)
{
    return syscall(SYS_write, fd, src, count);
}

ssize_t read(int fd, void *buf, size_t nbyte)
{
    return syscall(SYS_read, fd, buf, nbyte);
}

pid_t fork(void)
{
#if __linux__
    return syscall(SYS_fork);
#else
    return -ENOSYS;
#endif
}

int execv(const char *pathname, char *const argv[])
{
#if __linux__
    /* Linux does not support execv directly.
     * It supports the more general execve,
     * where the 'e' is for an additional "environment" argument. */
    return syscall(SYS_execve, pathname, argv, NULL);
#else
    (void) pathname, (void) argv; // Unused
    return -ENOSYS;
#endif
}

/** Custom process-spawn function for Munix */
int proc_spawn_munix(const char *pathname, char *const argv[])
{
#if __munix__
    return syscall(SYS_proc_spawn_munix, pathname, argv);
#else
    (void) pathname, (void) argv; // Unused
    return -ENOSYS;
#endif
}

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
    /* Linux does not support waitpid directly.
     * It supports the more general wait4,
     * where the '4' is for a fourth "usage" argument
     * that is a pointer to a struct where the OS can put process
     * CPU usage info. */
    return syscall(SYS_wait4, pid, wstatus, options, NULL);
}

