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

/* === Thread Allocation/Initialization === */

static struct thread *thread_alloc(void)
{
    for (int i = 0; i < THREAD_MAX; i++)
        if (!tcb[i].tid) return &tcb[i];

    return NULL;
}

static void thread_close(struct thread *t)
{
    pr_debug("destroying thread %d (%s)\n", t->tid, t->process->name);

    /* Remove from lists. */
    list_del(&t->process_threads);
    list_del(&t->queue);

    /* Clear struct. */
    *t = (struct thread){};
}

/* === Process Allocation/Initialization === */

struct process *process_alloc(void)
{
    for (int i = 0; i < PROCESS_MAX; i++)
        if (!pcb[i].pid) return &pcb[i];
    return NULL;
}

int process_load_path(struct process *p, const char *cwd, const char *path)
{
    int res, file_isopen = 0;

    /* Reset struct. */
    *p = (struct process){.pid = next_pid++};
    INIT_LIST_HEAD(&p->threads);
    p->threadct_active = 0;
    path_basename(p->name, DEBUGSTR_MAX, path);

    /* Set stack addresses. */
    p->ustack = USTACK_DFLT;
    /* Allocate matching kernel stack. */
    ptrdiff_t i = p - pcb;
    p->kstack   = (uintptr_t) kstacks[i] + KSTACKSZ;

    res = addrspc_init(&p->addrspc);
    if (res < 0) goto error;

        /* Use this address space.
     * We'll need to update the address space as we load the ELF segments. */
    pm_set_root(p->addrspc.root_entry);

    /* Make user stack writeable. */
    size_t    ustack_sz = PAGESZ;
    uintptr_t ustack_low =
            STACK_DIR == STACK_DOWN ? p->ustack - ustack_sz : p->ustack;
    res = addrspc_map(
            &p->addrspc, (void *) ustack_low, ustack_low, ustack_sz,
            PME_USER | PME_W
    );
    if (res < 0) goto error;

    /* Open file. */
    res = file_open_path(&p->execfile, cwd, path);
    if (res < 0) goto error;
    file_isopen = 1;

    /* Read ELF header. */
    Elf32_Ehdr ehdr;
    res = elf_read_ehdr32(&p->execfile, &ehdr);
    if (res < 0) goto error;
    p->start_addr = ehdr.e_entry;

    /* Load segments. */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        res = elf_read_phdr32(&p->execfile, &ehdr, i, &phdr);
        if (res < 0) goto error;

        /* Skip non-load segments. */
        if (phdr.p_type != PT_LOAD) continue;

        /* Set permissions for segment pages. */
        uintptr_t vaddr = ALIGN_DOWN(phdr.p_vaddr, PAGESZ);
        uintptr_t paddr = vaddr;
        size_t    size = ALIGN_UP(phdr.p_vaddr + phdr.p_memsz, PAGESZ) - vaddr;

        pme_t flags = PME_USER;
        if (phdr.p_flags & PF_W) flags |= PME_W;

        res = addrspc_map(&p->addrspc, (void *) vaddr, paddr, size, flags);
        if (res < 0) goto error;

            /* Load. */
        res = elf_load_seg32(&p->execfile, &phdr);
        if (res < 0) goto error;
    }

    return 0;

error:
    addrspc_cleanup(&p->addrspc);
    if (file_isopen) file_close(&p->execfile);
    return res;
}

void process_close(struct process *p)
{
    if (p == current_process) current_process = NULL;
    pm_set_root(kernel_addrspc.root_entry);
    addrspc_cleanup(&p->addrspc);
    file_close(&p->execfile);
    *p = (struct process){};
}

/* === Process Management === */

typedef int main_fn(int argc, char *argv[]);

/**
 * Push a string onto a stack
 *
 * Adjusts the stack pointer and returns a pointer to the start of the string.
 */
static char *push_str(ureg_t **stack, char *str)
{
    if (!str) return NULL;

    int   slots = ALIGN_UP(strlen(str) + 1, sizeof(ureg_t)) / sizeof(ureg_t);
    char *dst;

    switch (STACK_DIR) {
    case STACK_DOWN:
        *stack -= slots;       // Make room for string.
        dst = (char *) *stack; // Start of string is current position.
        break;
    case STACK_UP:
        dst = (char *) (*stack + 1); // Start of string is next slot.
        *stack += slots;             // Adjust stack to end of string.
        break;
    }

    strcpy(dst, str);
    return dst;
}

static void push_args(ureg_t **sp, int argc, char *argv[])
{
    switch (STACK_DIR) {
    case STACK_DOWN: {
        /* Push string data onto stack. */
        char *new_argv[argc];
        for (int i = 0; i < argc; i++) new_argv[i] = push_str(sp, argv[i]);

        /* Push a separating zero. */
        PUSH(*sp, 0);

        /* Push new argv[] pointers. */
        for (int i = argc - 1; i >= 0; i--) PUSH(*sp, (ureg_t) new_argv[i]);

        /* Push argc. */
        PUSH(*sp, argc);
        break;
    }

    default: {
        TODO();
        kernel_noreturn();
    }
    };
}

enum start_strategy {
    PSTART_CALL,
    PSTART_LAUNCH,
    PSTART_THREAD,
};

int process_start(struct process *p, int argc, char *argv[])
{
    current_process = p;

    push_args((ureg_t **) &p->ustack, argc, argv);

    int tid = thread_create(p, p->start_addr, p->ustack);
    if (tid < 0) return tid;

    schedule();
    return 0;
}

void process_kill(struct process *p)
{
    if (!p) return;
    pr_debug("killing process %d (%s)\n", p->pid, p->name);
    process_close(p);
    if (p == current_process) kernel_noreturn();
}

_Noreturn void process_exit(int status)
{
    struct process *p = current_process;
    struct thread  *t = current_thread;

    pr_info("process %d (%s) exited with status %d\n",
            p ? p->pid : 0,
            p ? p->name : "",
            status);

    if (t) {
        current_thread = NULL;
        thread_close(t);
    }

    process_close(p);
    kernel_noreturn();
}

ssize_t process_write(int fd, const void *src, size_t count)
{
    if (fd < 0 || fd >= FD_MAX) return -EBADF;
    if (!current_process || !current_process->fds[fd]) return -EBADF;
    return file_write(current_process->fds[fd], src, count);
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

    pr_debug("incoming runstate=%d start=%p ustack=%p kstack=%p\n",
         incoming->runstate,
         (void *)incoming->start_addr,
         (void *)incoming->ustack,
         (void *)incoming->kstack
    );

    current_thread  = incoming;
    current_process = incoming->process;

    cpu_user_kstack_set(incoming->kstack);
    pm_set_root(incoming->process->addrspc.root_entry);

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

    case RS_READY:
        cpu_task_restore(&incoming->saved_state, 1);

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
    struct thread *t = thread_alloc();
    if (!t) return -ENOMEM;

    ptrdiff_t idx = t - tcb;

    *t = (struct thread){
            .tid        = next_tid++,
            .process    = p,
            .ustack     = ustack,
            .kstack     = (uintptr_t) kstacks[idx] + KSTACKSZ,
            .start_addr = start_addr,
            .runstate   = RS_NEW,
    };

    INIT_LIST_HEAD(&t->process_threads);
    INIT_LIST_HEAD(&t->queue);

    list_add_tail(&t->process_threads, &p->threads);
    p->threadct_active++;

    sched_add(t);

    return t->tid;
}

_Noreturn void thread_exit(int status)
{
    pr_info(
            "thread %d (%s) exited with status %d\n",
            current_thread->tid,
            current_thread->process->name,
            status
    );

    current_thread->exit_status = status;
    current_thread->runstate    = RS_EXITED;
    current_thread->process->threadct_active--;

    if (current_thread->process->threadct_active == 0)
        process_exit(status);

    current_thread = NULL;
    schedule();

    kernel_noreturn();
}

int thread_join(pid_t tid)
{
    struct thread *t = NULL;

    for (int i = 0; i < THREAD_MAX; i++) {
        if (tcb[i].tid == tid) {
            t = &tcb[i];
            break;
        }
    }

    if (!t) return -ESRCH;
    if (!current_thread) return -ESRCH;
    if (t == current_thread) return -EDEADLK;

    while (t->runstate != RS_EXITED)
        thread_yield();

    thread_close(t);
    return 0;
}

int thread_yield(void)
{
    if (!current_thread) return -ESRCH;

    current_thread->yield_ct++;
    schedule();
    return 0;
}

int thread_preempt(void)
{
    if (!current_thread) return -ESRCH;

    current_thread->preempt_ct++;
    pr_debug("preempt thread %d count %d\n",
             current_thread->tid, current_thread->preempt_ct);

    schedule();
    return 0;
}