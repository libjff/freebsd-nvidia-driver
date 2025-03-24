/*
 * SPDX-FileCopyrightText: Copyright (c) 2016 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Implementation of single worker thread queues for BSD. This provides a wrapper
 * around the taskqueue subsystem.
 */

#include <sys/types.h>
#include <machine/atomic.h>
#include "nv-kthread-q.h"

#define unlikely(x) __builtin_expect(!!(x), 0)

int
nv_kthread_q_init_on_node(nv_kthread_q_t *q,
                          const char *qname,
                          int preferred_node)
{
    /*
     * taskqueue_thread_enqueue is the function that will be called to add something
     * to this taskqueue. It expects a taskqueue ** pointer, so we pass &queue->tq
     * as the context argument. We use a fast taskqueue so this can be used from
     * interrupt contexts.
     */
    q->tq = taskqueue_create_fast(qname, M_WAITOK, taskqueue_thread_enqueue,
                                  &q->tq);
    if (!q->tq)
        return 1;

    /*
     * On newer version of FreeBSD we have the cpuset option for handling
     * pinning a thread to a number of CPUs. If this is available and the
     * caller specified a perferred node, then we can use it. Otherwise just
     * launch a kernel thread without CPU preference.
     */
#if __FreeBSD_version >= 1100061
    if (preferred_node != NV_KTHREAD_NO_NODE) {
        /* This gets the cpu id from the per-CPU data */
        uint32_t cpuid = PCPU_GET(cpuid);
        cpuset_t cpuset = CPUSET_T_INITIALIZER(cpuid);
        taskqueue_start_threads_cpuset(&q->tq, 1, PWAIT, &cpuset, "%s taskq", qname);
        return 0;
    }
#endif

    /* Launch one thread in the pool (for now) */
    taskqueue_start_threads(&q->tq, 1, PWAIT, "%s taskq", qname);
    return 0;
}

int nv_kthread_q_init(nv_kthread_q_t *q, const char *qname)
{
    return nv_kthread_q_init_on_node(q, qname, NV_KTHREAD_NO_NODE);
}

void
nv_kthread_q_stop(nv_kthread_q_t *q)
{
    taskqueue_free(q->tq);
    q->tq = NULL;
}

void
nv_kthread_q_flush(nv_kthread_q_t *q)
{
    taskqueue_drain_all(q->tq);
}

static void
nv_kthread_task_callback(void *context, int pending)
{
    nv_kthread_q_item_t *q_item = context;
    // Unset pending using a CAS operation
    if (unlikely(!atomic_cmpset_rel_int(&q_item->pending, 1, 0)))
        printf("NVIDIA: nv_kthread_q failed to clear pending status for task");

    q_item->function_to_run(q_item->function_args);
}

void
nv_kthread_q_item_init(nv_kthread_q_item_t *q_item,
                       nv_q_func_t function_to_run,
                       void *function_args)
{
    q_item->function_to_run = function_to_run;
    q_item->function_args = function_args;
    atomic_store_int(&q_item->pending, 0);
    TASK_INIT(&q_item->task, 0, nv_kthread_task_callback, q_item);
}

int
nv_kthread_q_schedule_q_item(nv_kthread_q_t *q,
                             nv_kthread_q_item_t *q_item)
{
    // Fail to enqueue if this item is already enqueued
    //
    // Use a CAS to ensure that pending is flipped from 0 to 1
    if (!atomic_cmpset_acq_int(&q_item->pending, 0, 1))
        return 0;

    // Ensure this returns success (0) before we return our success (1)
    //
    // This will return EPIPE if the queue is being freed
    if (taskqueue_enqueue(q->tq, &q_item->task) == 0) {
        return 1;
    } else {
        return 0;
    }
}

int
nv_kthread_q_run_self_test(void)
{
    return -1;
}
