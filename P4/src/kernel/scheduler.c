//#define LOG_LEVEL LOG_DEBUG
//#define LOG_LEVEL LOG_TRACE

#include "scheduler.h"

#include "kernel.h"

#include <drivers/log.h>

#include <core/list.h>
#include <core/sprintf.h>

struct list_head ready_queue;

static int tqueue_tostr(char *buf, size_t n, struct list_head *queue)
{
    char *pos = buf, *end = buf + n;

    struct thread *t;
    list_for_each_entry(t, queue, queue)
    {
        pos += snprintf(pos, BUFREM(pos, end), "%u -> ", t->tid);
    }
    pos += snprintf(pos, BUFREM(pos, end), "(end)");
    return pos - buf;
}

void sched_add(struct thread *t)
{
    pr_debug("sched_add: tid=%d (%s)\n", t->tid, t->process->name);
    list_add_tail(&t->queue, &ready_queue);
}

void sched_remove(struct thread *t)
{
    pr_debug("sched_remove: tid=%d (%s)\n", t->tid, t->process->name);
    list_del(&t->queue);
}

static struct thread *choose_next_thread(void)
{
    for (;;) {
        const size_t DBGSZ = 256;
        char         dbgbuf[DBGSZ];
        pr_trace(
                "ready_queue: %s\n",
                (tqueue_tostr(dbgbuf, DBGSZ, &ready_queue), dbgbuf)
        );

        struct thread *t =
                list_first_entry_or_null(&ready_queue, struct thread, queue);

        if (!t) {
            pr_error("ready_queue empty; no threads to run\n");
            kernel_noreturn();
        }

        switch (t->runstate) {
        case RS_NEW:
        case RS_READY: return t;
        default:
            pr_debug(
                    "skipping thread %d (%s) with non-run state %d\n", t->tid,
                    t->process->name, t->runstate
            );
            list_rotate_left(&ready_queue);
            break;
        };
    }
}

void schedule(void)
{
    if (current_thread && current_thread->runstate == RS_READY)
        list_add_tail(&current_thread->queue, &ready_queue);

    struct thread *next = choose_next_thread();
    sched_remove(next);

    if (next == current_thread) return;

    thread_switch(current_thread, next);
}

int init_scheduler(void)
{
    INIT_LIST_HEAD(&ready_queue);
    return 0;
}

