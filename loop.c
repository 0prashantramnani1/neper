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

#include <runtime/sync.h>
#include <runtime/tcp.h>

#define NETPERF_PORT	8000
#define BUF_SIZE	32768

static void handler_stop(struct flow *f, uint32_t events)
{
        struct thread *t = flow_thread(f);
        t->stop = 1;
}

/*
 * The main event loop, used by both clients and servers. Calls various init
 * functions and then processes events until the thread is marked as stopped.
 */

void *loop(struct thread_neper *t)
{
	printf("In main LOOP \n");
        //const struct options *opts = t->opts;
        //struct epoll_event *events;
	/*
        const struct flow_create_args args = {
                .thread  = t,
                .fd      = t->stop_efd,
                .events  = EPOLLIN,
                .opaque  = NULL,
                .handler = handler_stop,
                .mbuf_alloc = NULL,
                .stat    = NULL
        };

        flow_create(&args);
	*/
        /* Server sockets must be created in order
         * so that the ebpf filter works.
         * Client sockets don't need to be so but we apply this logic anyway.
         * Wait for its turn to do fn_loop_init() according to t->index.
         */
	/*
        pthread_mutex_lock(t->loop_init_m);
        while (*t->loop_inited < t->index)
                pthread_cond_wait(t->loop_init_c, t->loop_init_m);
        t->fn->fn_loop_init(t);
        (*t->loop_inited)++;
        pthread_cond_broadcast(t->loop_init_c);
        pthread_mutex_unlock(t->loop_init_m);

        events = calloc_or_die(opts->maxevents, sizeof(*events), t->cb);
	*/
        /* support for rate limited flows */
        //t->rl.pending_flows = calloc_or_die(t->flow_limit, sizeof(struct flow *), t->cb);
        //t->rl.next_event = ~0ULL; /* no pending timeouts */
        //t->rl.pending_count = 0; /* no pending flows */
        //pthread_barrier_wait(t->ready);
	//barrier_wait(t->ready);
        //while (!t->stop) {
        //        /* Serve pending event, compute timeout to next event */
        //        int ms = flow_serve_pending(t);
        //        int nfds = epoll_wait(t->epfd, events, opts->maxevents, ms);
        //        int i;

        //        if (nfds == -1) {
        //                if (errno == EINTR)
        //                        continue;
        //                PLOG_FATAL(t->cb, "epoll_wait");
        //        }
        //        for (i = 0; i < nfds && !t->stop; i++)
        //                flow_event(&events[i]);
        //}
	printf("1.L\n");
	int ret;
	tcpqueue_t *q;
	struct netaddr laddr;
	laddr.ip = 0;
	laddr.port = NETPERF_PORT;

	if(tcp_listen(laddr, 4096, &q)!=0) {
		printf("Error in listening to local port \n");
	}
	else printf("Listening on local port \n");
	while (true) {
		printf("in while loop \n");
		tcpconn_t *c;

		if(tcp_accept(q, &c) == 0) {
			printf("Error in tcp_accept \n");
		}
		printf("ret after accept: %d", ret);
		//ret = thread_spawn(server_worker, c);
		unsigned char buf[BUF_SIZE];
		//tcpconn_t *c = (tcpconn_t *)arg;
		ssize_t ret;

		while (true) {
			ret = tcp_read(c, buf, BUF_SIZE);
			if (ret <= 0)
				break;

			ret = tcp_write(c, buf, ret);
			if (ret < 0)
				break;
		}

	}
	printf("Exited main while loop \n");
       // thread_flush_stat(t);
        //free(events);
        //do_close(t->epfd);

        /* TODO: The first flow object is leaking here... */

        /* This is technically a thread callback so it must return a (void *) */
	barrier_wait(t->ready);
        return NULL;
}
