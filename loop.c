/*
 * Copyright 2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common.h"
#include "flow.h"
#include "loop.h"
#include "socket.h"
#include "thread.h"

// CALADAN
// #include <runtime/sync.h>
// #include <runtime/tcp.h>
// #include <runtime/poll.h>

#define NETPERF_PORT	8000
#define BUF_SIZE	32768

static void handler_stop(struct flow *f, uint32_t events)
{
        printf("Handler Stop\n");
        struct thread_neper *t = flow_thread(f);
        t->stop = 1;
}

/*
 * The main event loop, used by both clients and servers. Calls various init
 * functions and then processes events until the thread is marked as stopped.
 */

void *loop(struct thread_neper *t)
{
	printf("In main LOOP with thread_id: %d\n", t->index);
        const struct options *opts = t->opts;

        //CALADAN
        poll_trigger_t **events;

	
        const struct flow_create_args args = {
                .thread  = t,
                // .fd      = t->stop_efd,
                .trigger = t->stop_trigger,
                .events  = SEV_READ,
                .opaque  = NULL,
                .handler = handler_stop,
                .mbuf_alloc = NULL,
                .stat    = NULL
        };

        flow_create(&args);
	
        /* Server sockets must be created in order
         * so that the ebpf filter works.
         * Client sockets don't need to be so but we apply this logic anyway.
         * Wait for its turn to do fn_loop_init() according to t->index.
         */
	
        mutex_lock(t->loop_init_m);
        while (*t->loop_inited < t->index)
                condvar_wait(t->loop_init_c, t->loop_init_m);
        t->fn->fn_loop_init(t);
        (*t->loop_inited)++;
        condvar_broadcast(t->loop_init_c);
        mutex_unlock(t->loop_init_m);

        t->total_reqs=0;
        // CALADAN
        // initialising triggers/events
        events = calloc(opts->maxevents, sizeof(poll_trigger_t *));
        
        /* support for rate limited flows */
        //t->rl.pending_flows = calloc_or_die(t->flow_limit, sizeof(struct flow *), t->cb);
        //t->rl.next_event = ~0ULL; /* no pending timeouts */
        //t->rl.pending_count = 0; /* no pending flows */
        barrier_wait(t->ready);
        
        poll_trigger_t *last_trigger = NULL;
        printf("Starting the event Loop for thread_id: %d\n", t->index);        
        while (!t->stop) {      
                int nfds = poll_return_triggers(t->waiter, events, opts->maxevents);

                if (nfds == -1) {
                        if (errno == EINTR)
                                continue;
                        PLOG_FATAL(t->cb, "epoll_wait");
                }
                for (int i = 0; i < nfds && !t->stop; i++) {
                        flow_event(events[i]);
                }
        }
        printf("Event Loop completed for thread_id: %d\n", t->index);
        printf("Total events recorded by the thread_id: %d - %lld\n", t->index, t->total_reqs);
        thread_flush_stat(t);
        free(events);
        // printf("thread_stats_snaps1: %d\n", thread_stats_snaps(t));
        // printf("thread_stats_flows1: %d\n", thread_stats_flows(t));

        // TODO: Close tcpqueue
        //do_close(t->epfd);

        /* TODO: The first flow object is leaking here... */

        /* This is technically a thread callback so it must return a (void *) */
        thread_exit();
        // return NULL;
}
