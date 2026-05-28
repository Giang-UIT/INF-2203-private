//#define LOG_LEVEL LOG_DEBUG
//#define LOG_LEVEL LOG_TRACE

#include "process.h"

#include "kernel.h"
#include "scheduler.h"

#include <abi.h>
#include <cpu.h>    
#include <cpu_pagemap.h>

#include <drivers/fileformat/elf.h>
#include <drivers/log.h>
#include <drivers/vfs.h>

#include <core/compiler.h>
#include <core/errno.h>
#include <core/inttypes.h>
#include <core/macros.h>
#include <core/path.h>
#include <core/sprintf.h>
#include <core/string.h>

#define PROCESS_MAX 8
static struct process pcb[PROCESS_MAX];
static pid_t          next_pid = 1;
struct process *current_process;

#define THREAD_MAX 16
static struct thread tcb[THREAD_MAX];
static pid_t         next_tid = 1;
struct thread       *current_thread;

#define KSTACKSZ 4096
unsigned char kstacks[THREAD_MAX][KSTACKSZ] ATTR_ALIGNED(KSTACKSZ);

#define ARGVSZ 16

/* === Thread Allocation/Initialization === */

static struct thread *thread_alloc(void)
{
    for (int i = 0; i < THREAD_MAX; i++)
        if (!tcb[i].tid) return &tcb[i];
    return NULL;
}

static int thread_init(
        struct thread  *t,
        struct process *p,
        uintptr_t       start_addr,
        uintptr_t       ustack
)
{
    *t = (struct thread){
            .process    = p,
            .tid        = next_tid++,
            .start_addr = start_addr,
            .ustack     = ustack,
            .runstate   = RS_NEW,
    };

    ptrdiff_t i = t - tcb;
    t->kstack   = (uintptr_t) kstacks[i] + KSTACKSZ;

    p->threadct_active++;
    list_add_tail(&t->process_threads, &p->threads);

    sched_add(t);
    return 0;
}

static void thread_close(struct thread *t)
{
    pr_debug("destroying thread %d (%s)\n", t->tid, t->process->name);

    list_del(&t->process_threads);
    list_del(&t->queue);

    *t = (struct thread){};
}

/* === Process Allocation/Initialization === */

struct process *process_alloc(void)
{
    for (int i = 0; i < PROCESS_MAX; i++)
        if (!pcb[i].pid) return &pcb[i];
    return NULL;
}

static struct process *process_find_pid(pid_t pid)
{
    for (int i = 0; i < PROCESS_MAX; i++)
        if (pcb[i].pid == pid) return &pcb[i];
    return NULL;
}

int process_load_path(struct process *p, const char *cwd, const char *path)
{
    int   res, file_isopen = 0;
    pme_t old_root = pm_get_root();

    *p = (struct process){
            .pid         = next_pid++,
            .parent_pid  = current_process ? current_process->pid : 0,
            .state       = PS_RUNNING,
            .exit_status = 0,
    };
    INIT_LIST_HEAD(&p->threads);
    path_basename(p->name, DEBUGSTR_MAX, path);

    p->ustack = USTACK_DFLT;

    res = addrspc_init(&p->addrspc);
    if (res < 0) goto error;

    pm_set_root(p->addrspc.root_entry);

    size_t    ustack_sz  = PAGESZ;
    uintptr_t ustack_low =
            STACK_DIR == STACK_DOWN ? p->ustack - ustack_sz : p->ustack;

    res = addrspc_map_alloc(
            &p->addrspc, (void *) ustack_low, ustack_sz, PME_USER | PME_W
    );
    if (res < 0) goto error;

    res = file_open_path(&p->execfile, cwd, path);
    if (res < 0) goto error;
    file_isopen = 1;

    Elf32_Ehdr ehdr;
    res = elf_read_ehdr32(&p->execfile, &ehdr);
    if (res < 0) goto error;
    p->start_addr = ehdr.e_entry;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        res = elf_read_phdr32(&p->execfile, &ehdr, i, &phdr);
        if (res < 0) goto error;

        if (phdr.p_type != PT_LOAD) continue;

        uintptr_t vaddr = ALIGN_DOWN(phdr.p_vaddr, PAGESZ);
        size_t    size =
                ALIGN_UP(phdr.p_vaddr + phdr.p_memsz, PAGESZ) - vaddr;

        pme_t flags = PME_USER;
        if (phdr.p_flags & PF_W) flags |= PME_W;

        res = addrspc_map_alloc(&p->addrspc, (void *) vaddr, size, flags);
        if (res < 0) goto error;

        res = elf_load_seg32(&p->execfile, &phdr);
        if (res < 0) goto error;
    }

    pm_set_root(old_root);
    return 0;

error:
    pm_set_root(old_root);
    addrspc_cleanup(&p->addrspc);
    if (file_isopen) file_close(&p->execfile);
    return res;
}

void process_close(struct process *p)
{
    struct thread *t, *n;
    list_for_each_entry_safe(t, n, &p->threads, process_threads)
    {
        thread_close(t);
    }
    if (p == current_process) current_process = NULL;
    addrspc_cleanup(&p->addrspc);
    file_close(&p->execfile);
    *p = (struct process){};
}

/* === Process Management === */

typedef int main_fn(int argc, char *argv[]);

static char *push_str(ureg_t **stack, char *str)
{
    if (!str) return NULL;

    int   slots = ALIGN_UP(strlen(str) + 1, sizeof(ureg_t)) / sizeof(ureg_t);
    char *dst;

    switch (STACK_DIR) {
    case STACK_DOWN:
        *stack -= slots;
        dst = (char *) *stack;
        break;
    case STACK_UP:
        dst = (char *) (*stack + 1);
        *stack += slots;
        break;
    }

    strcpy(dst, str);
    return dst;
}

static void push_args(ureg_t **sp, int argc, char *argv[])
{
    switch (STACK_DIR) {
    case STACK_DOWN: {
        char *new_argv[argc];
        for (int i = 0; i < argc; i++) new_argv[i] = push_str(sp, argv[i]);

        PUSH(*sp, 0);

        for (int i = argc - 1; i >= 0; i--) PUSH(*sp, (ureg_t) new_argv[i]);

        PUSH(*sp, argc);
        break;
    }

    default: {
        TODO();
        kernel_noreturn();
    }
    };
}

static int copy_argv(
        char              *dstbuf,
        size_t             dstbufsz,
        char              *dst[],
        size_t             dstsz,
        const char *const  argv[]
)
{
    char *pos = dstbuf, *end = dstbuf + dstbufsz;
    for (size_t i = 0; i < dstsz && pos < end; i++) {
        if (!argv[i]) {
            dst[i] = NULL;
            return i;
        }

        dst[i] = pos;
        pos += snprintf(pos, BUFREM(pos, end), "%s", argv[i]);
        pos++;
    }

    return -ENOMEM;
}

enum start_strategy {
    PSTART_CALL,
    PSTART_LAUNCH,
    PSTART_THREAD,
};

int process_start(struct process *p, int argc, char *argv[])
{
    enum start_strategy start_strat = PSTART_THREAD;

    switch (start_strat) {
    case PSTART_CALL: {
        current_process = p;
        main_fn *entry = (void *) p->start_addr;
        pr_debug("%s: calling to %p to start ...\n", argv[0], entry);
        int res = entry(argc, argv);
        pr_debug("%s: returned %d\n", argv[0], res);
        return res;
    }

    case PSTART_LAUNCH: {
        pme_t old_root = pm_get_root();
        pm_set_root(p->addrspc.root_entry);
        push_args((ureg_t **) &p->ustack, argc, argv);
        pm_set_root(old_root);
        TODO();
        kernel_noreturn();
    }

    case PSTART_THREAD: {
        pme_t old_root = pm_get_root();
        pm_set_root(p->addrspc.root_entry);
        push_args((ureg_t **) &p->ustack, argc, argv);
        pm_set_root(old_root);

        return thread_create(p, p->start_addr, p->ustack);
    }
    };

    return -ENOTSUP;
}

int process_spawn(const char *pathname, char *const argv[])
{
    int res;

    if (!pathname || !argv) return -EINVAL;
    if (!current_process) return -ESRCH;

    struct process *child = process_alloc();
    if (!child) return -ENOMEM;

    char  pathbuf[PATH_MAX];
    char  argbuf[256];
    char *kargv[ARGVSZ];

    snprintf(pathbuf, sizeof(pathbuf), "%s", pathname);

    int argc = copy_argv(
            argbuf, sizeof(argbuf), kargv, ARRAY_SIZE(kargv),
            (const char *const *) argv
    );
    if (argc < 0) return argc;

    res = process_load_path(child, "/", pathbuf);
    if (res < 0) return res;

    child->parent_pid = current_process->pid;

    for (int i = 0; i < FD_MAX; i++) child->fds[i] = current_process->fds[i];

    pme_t old_root = pm_get_root();
    pm_set_root(child->addrspc.root_entry);
    push_args((ureg_t **) &child->ustack, argc, kargv);
    pm_set_root(old_root);

    res = thread_create(child, child->start_addr, child->ustack);
    if (res < 0) {
        process_close(child);
        return res;
    }

    pr_debug("process_spawn: spawned pid=%d (%s)\n", child->pid, child->name);
    return child->pid;
}

void process_kill(struct process *p)
{
    if (!p) return;
    pr_info("killing process %d (%s)\n", p->pid, p->name);
    process_close(p);
    kernel_noreturn();
}

_Noreturn void process_exit(int status)
{
    pr_info("process %d (%s) exited with status %d\n", current_process->pid,
            current_process->name, status);

    current_process->exit_status = status;
    current_process->state       = PS_EXITED;

    if (current_thread) {
        pr_debug("process_exit: marking tid=%d exited\n", current_thread->tid);
        current_thread->exit_status = status;
        current_thread->runstate    = RS_EXITED;
    }

    current_process->threadct_active = 0;

    pr_debug("process_exit: scheduling away from exited process %d\n",
             current_process->pid);

    schedule();

    pr_error("process_exit: returned to exited process %d (%s)\n",
             current_process->pid, current_process->name);
    kernel_noreturn();
}

int process_wait(pid_t pid, int *wstatus, int options)
{
    UNUSED(options);

    if (!current_process) return -ESRCH;

    struct process *child = process_find_pid(pid);
    if (!child) return -ESRCH;

    if (child->parent_pid != current_process->pid) return -ESRCH;

    pr_debug("process_wait: pid=%d waiting for child=%d\n",
             current_process->pid, pid);

    while (child->state != PS_EXITED) {
        pr_debug("process_wait: child=%d still running, yielding\n", pid);
        thread_yield();
    }

    if (wstatus) *wstatus = child->exit_status;

    pr_debug("process_wait: child=%d exited status=%d\n",
             pid, child->exit_status);

    process_close(child);
    return pid;
}

ssize_t process_write(int fd, const void *src, size_t count)
{
    if (fd < 0 || fd >= FD_MAX) return -EBADF;
    if (!current_process || !current_process->fds[fd]) return -EBADF;
    return file_write(current_process->fds[fd], src, count);
}

ssize_t process_read(int fd, void *dst, size_t count)
{
    if (fd < 0 || fd >= FD_MAX) return -EBADF;
    if (!current_process || !current_process->fds[fd]) return -EBADF;
    return file_read(current_process->fds[fd], dst, count);
}

/* === Thread Management === */

int thread_switch(struct thread *outgoing, struct thread *incoming)
{
    if (!incoming) return -ESRCH;

    pr_debug(
            "switching threads: %d (%s) -> %d (%s)\n",
            outgoing ? outgoing->tid : 0,
            outgoing ? outgoing->process->name : "none", incoming->tid,
            incoming->process->name
    );

    current_thread  = incoming;
    current_process = incoming->process;

    pm_set_root(incoming->process->addrspc.root_entry);

    cpu_user_kstack_set((uintptr_t) incoming->kstack);

    if (outgoing && cpu_task_save(&outgoing->saved_state) != 0) {
        pr_trace("%d (%s) resumed\n", outgoing->tid, outgoing->process->name);
        return 0;
    } else if (outgoing) {
        pr_trace("%d (%s) saved\n", outgoing->tid, outgoing->process->name);
    }

    switch (incoming->runstate) {
    case RS_NEW:
        incoming->runstate = RS_READY;
        cpu_user_start(incoming->start_addr, incoming->ustack);
        kernel_noreturn();

    case RS_READY:
        cpu_task_restore(&incoming->saved_state, 1);
        kernel_noreturn();

    default:
        pr_error(
                "%s: incoming thread %d (%s) has non-run state %d\n", __func__,
                incoming->tid, incoming->process->name, incoming->runstate
        );
    }

    kernel_noreturn();
}

int thread_create(struct process *p, uintptr_t start_addr, uintptr_t ustack)
{
    int res;

    struct thread *t = thread_alloc();
    if (!t) return -ENOMEM;
    res = thread_init(t, p, start_addr, ustack);
    if (res < 0) goto error;

    pr_debug(
            "thread %d (%s) created: start_addr=%p, ustack=%p, kstack=%p\n",
            t->tid, p->name, (void *) t->start_addr, (void *) t->ustack,
            (void *) t->kstack
    );
    return t->tid;

error:
    if (t) thread_close(t);
    return res;
}

_Noreturn void thread_exit(int status)
{
    pr_debug(
            "thread %d (%s) exited with status %d\n", current_thread->tid,
            current_thread->process->name, status
    );

    current_thread->exit_status = status;
    current_thread->runstate    = RS_EXITED;

    current_thread->process->threadct_active--;
    if (current_thread->process->threadct_active == 0) {
        pr_debug(
                "process %d (%s) last thread exited\n",
                current_thread->process->pid, current_thread->process->name
        );
        process_exit(status);
    }

    schedule();

    pr_error(
            "Returned to exited thread %d (%s).\n", current_thread->tid,
            current_thread->process->name
    );
    kernel_noreturn();
}

static struct thread *process_find_thread(struct process *p, pid_t tid)
{
    struct thread *t;
    list_for_each_entry(t, &p->threads, process_threads)
    {
        if (t->tid == tid) return t;
    }
    return NULL;
}

int thread_join(pid_t tid)
{
    struct thread *t = process_find_thread(current_process, tid);
    if (!t) {
        pr_error("process %s has no thread %d\n", current_process->name, tid);
        return -ESRCH;
    }

    while (t->runstate != RS_EXITED) {
        thread_yield();
    }

    pr_debug(
            "thread %d (%s) joined thread %d with exit status %d\n",
            current_thread->tid, current_thread->process->name, t->tid,
            t->exit_status
    );
    int exit_status = t->exit_status;
    thread_close(t);
    return exit_status;
}

int thread_yield(void)
{
    pr_debug("thread_yield: tid=%d (%s)\n",
             current_thread->tid, current_thread->process->name);
    current_thread->yield_ct++;
    schedule();
    return 0;
}

int thread_preempt(void)
{
    current_thread->preempt_ct++;
    schedule();
    return 0;
}
