#ifndef THREADS_H
#define THREADS_H

#include <core/compiler.h>
#include <core/errno.h>
#include <core/types.h>

#include <stdatomic.h>
#include <stdint.h>

/* === Threads === */

struct _thrd {
    pid_t       ktid;
    atomic_flag inuse;
    uintptr_t   ustack_base;
#if __linux__
    volatile uint32_t futex;
#endif
};

enum _thrd_res {
    thrd_success = 0,
    thrd_error   = -EFAULT,
    thrd_busy    = -EBUSY,
    thrd_nomem   = -ENOMEM,
};

typedef struct _thrd *thrd_t; ///< Type for a user-side thread identifier
typedef int (*thrd_start_t)(void *); ///< Type for a thread start function

int            thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
_Noreturn void thrd_exit(int res);
int            thrd_join(thrd_t thr);

void thrd_yield(void);

/* === Mutexes === */

struct _mtx {
    atomic_flag    flag;
};

typedef struct _mtx mtx_t; ///< Type for a user-side mutex

/** Mutex type */
enum _mtx_type {
    mtx_plain = 1, ///< Normal mutex
};

int  mtx_init(mtx_t *mutex, int type);
void mtx_destroy(mtx_t *mutex);
int  mtx_trylock(mtx_t *mutex);
int  mtx_lock(mtx_t *mutex);
int  mtx_unlock(mtx_t *mutex);

/* === Condition Variables === */

#define _CND_QUEUE_SZ 8

struct _cnd {
    volatile unsigned seq;
};

typedef struct _cnd cnd_t; ///< Type for a user-side condition variable

int  cnd_init(cnd_t *cond);
void cnd_destroy(cnd_t *cond);
int  cnd_wait(cnd_t *cond, mtx_t *mutex);
int  cnd_signal(cnd_t *cond);
int  cnd_broadcast(cnd_t *cond);

/* === Barriers ===
 * N.B. non-standard API */

#define __MUNIX_BARRIERS__

struct _brr {
    size_t target;
    size_t arrived;
    unsigned generation;
    mtx_t lock;
    cnd_t cond;
};

typedef struct _brr brr_t;

int brr_init(brr_t *b, size_t n);
int brr_destroy(brr_t *b);
int brr_wait(brr_t *b);

#endif /* THREADS_H */
