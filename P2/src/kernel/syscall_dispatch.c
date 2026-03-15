#include "process.h"

#include <cpu.h>
#include <sys/syscall.h>

#include <drivers/log.h>

#include <core/errno.h>
#include <core/macros.h>

long syscall_dispatch(
        long   number,
        ureg_t arg1,  
        ureg_t arg2,  
        ureg_t arg3, 
        ureg_t arg4,   
        ureg_t arg5  
)
{
    pr_debug("syscall %ld from process %s\n", number, current_process->name);

    switch ((enum syscall_nr) number) {
    case SYS_NULL:
    case SYS_exit: {
        pr_info("process %d (%s) called SYS_exit\n", current_process->pid,
                current_process->name);
        process_exit(-1);
    }
    case SYS_write: {
        process_write(arg1, (char *) arg2, arg3);
        return 0;
    }

    case SYS_MAX: break;

    }

    pr_info("unhandled syscall %ld from process %d (%s)\n", number,
            current_process->pid, current_process->name);
    return -ENOSYS;
}
