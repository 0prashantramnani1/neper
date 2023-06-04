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

#define _GNU_SOURCE

#include "common.h"
#include "flow.h"
#include "loop.h"
#include "socket.h"
#include "thread.h"

#include <stdio.h>
#include <stdlib.h>
//#include <papi.h>
#include <pthread.h>
#include <time.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <linux/hw_breakpoint.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <unistd.h>
#include <string.h>

// CALADAN
// #include <runtime/sync.h>
// #include <runtime/tcp.h>
// #include <runtime/poll.h>

#define NETPERF_PORT	8000
#define BUF_SIZE	32768

#define NUM_EVENTS 2

static long
perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
        int ret;

        ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                        group_fd, flags);
        return ret;
}

unsigned long thread_index() {
	printf("THREAD INDEX: %d\n", Idx);
        return Idx;
}

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

        // CALADAN
        // initialising triggers/events
        events = calloc(opts->maxevents, sizeof(poll_trigger_t *));
        t->total_reqs = 0;
        /* support for rate limited flows */
        //t->rl.pending_flows = calloc_or_die(t->flow_limit, sizeof(struct flow *), t->cb);
        //t->rl.next_event = ~0ULL; /* no pending timeouts */
        //t->rl.pending_count = 0; /* no pending flows */
        // barrier_wait(t->ready);
        
        // poll_trigger_t *last_trigger = NULL;

//////////////////////////// PAPI //////////////////////////////////////////////


        /*
        // Initialising PAPI
        int retval;
	
	int EventSet = PAPI_NULL;
	unsigned long long values[NUM_EVENTS];
	retval = PAPI_create_eventset(&EventSet);
        
	if (retval != PAPI_OK) {
		printf("PAPI create event set failed\n");
                printf("Error: %d\n", retval);
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
	} else {
                printf("PAP create event Success\n");
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
        }
        
	int Events[NUM_EVENTS]={ PAPI_TOT_CYC, PAPI_TOT_INS};
        //, PAPI_L3_DCM, PAPI_L3_ICM};
        retval = PAPI_add_events (EventSet, Events, NUM_EVENTS);
        
        
	if (retval != PAPI_OK) {
		printf("PAPI add events failed\n");
                printf("Error: %d\n", retval);
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
	}  else {
                printf("PAP add Success\n");
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
        }
       	for(int i=0;i<100000000;i++); 
       	retval = PAPI_start (EventSet);

        
	if (retval != PAPI_OK) {
		printf("PAP start failed\n");
                printf("Error: %d\n", retval);
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
	} else {
                printf("PAP start Success\n");
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
        }
        */
        int fd_cyc1, fd_cyc2, fd_instr1, fd_instr2;
        // if(t->index == 0) {
                /*
                int retval;
                unsigned long int tid;

                struct perf_event_attr pe_cyc1, pe_cyc2, pe_instr1, pe_instr2;

                memset(&pe_cyc1, 0, sizeof(pe_cyc1));
                memset(&pe_cyc2, 0, sizeof(pe_cyc2));
                memset(&pe_instr1, 0, sizeof(pe_instr1));
                memset(&pe_instr2, 0, sizeof(pe_instr2));


                ////// CORE 1 ////////////////
                pe_cyc1.type = PERF_TYPE_HARDWARE;
                pe_cyc1.size = sizeof(pe_cyc1);
                pe_cyc1.config = PERF_COUNT_HW_CPU_CYCLES;
                pe_cyc1.disabled = 1;

                pe_instr1.type = PERF_TYPE_HARDWARE;
                pe_instr1.size = sizeof(pe_instr2);
                pe_instr1.config = PERF_COUNT_HW_INSTRUCTIONS;
                pe_instr1.disabled = 1;

                fd_cyc1 = perf_event_open(&pe_cyc1, -1, 1, -1, 0);
                if (fd_cyc1 == -1) {
                        fprintf(stderr, "Error opening fd_cyc1 %llx\n", pe_cyc1.config);
                }

                fd_instr1 = perf_event_open(&pe_instr1, -1, 1, -1, 0);
                if (fd_instr1 == -1) {
                        fprintf(stderr, "Error opening fd_instr1 %llx\n", pe_cyc1.config);
                }

                ////// CORE 2 ////////////////
                pe_cyc2.type = PERF_TYPE_HARDWARE;
                pe_cyc2.size = sizeof(pe_cyc2);
                pe_cyc2.config = PERF_COUNT_HW_CPU_CYCLES;
                pe_cyc2.disabled = 1;

                pe_instr2.type = PERF_TYPE_HARDWARE;
                pe_instr2.size = sizeof(pe_instr2);
                pe_instr2.config = PERF_COUNT_HW_INSTRUCTIONS;
                pe_instr2.disabled = 1;

                fd_cyc2 = perf_event_open(&pe_cyc2, -1, 25, -1, 0);
                if (fd_cyc2 == -1) {
                        fprintf(stderr, "Error opening fd_cyc2 %llx\n", pe_cyc1.config);
                }

                fd_instr2 = perf_event_open(&pe_instr2, -1, 25, -1, 0);
                if (fd_instr2 == -1) {
                        fprintf(stderr, "Error opening fd_instr2 %llx\n", pe_cyc1.config);
                }
                */
        // }
	

        barrier_wait(t->ready);
        

        if(t->index == 0) {
                //system("perf stat -e cycles,instructions,l3_comb_clstr_state.request_miss -C 1,25 -o perf_output.txt&");
		system("perf record -F 1000 --call-graph dwarf,16385 -C 1,25&");
                /*
                ioctl(fd_cyc1, PERF_EVENT_IOC_RESET, 0);
                ioctl(fd_cyc1, PERF_EVENT_IOC_ENABLE, 0);

                ioctl(fd_instr1, PERF_EVENT_IOC_RESET, 0);
                ioctl(fd_instr1, PERF_EVENT_IOC_ENABLE, 0);

                ioctl(fd_cyc2, PERF_EVENT_IOC_RESET, 0);
                ioctl(fd_cyc2, PERF_EVENT_IOC_ENABLE, 0);

                ioctl(fd_instr2, PERF_EVENT_IOC_RESET, 0);
                ioctl(fd_instr2, PERF_EVENT_IOC_ENABLE, 0);
                */
        }

/////////////////////////////////////////////////////////////////////////////
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
        printf("Total events recorded by the thread_id: %d - %lld\n", t->index, t->total_reqs);
        barrier_wait(t->papi_end);

        ////////////////////////////////////////////
        /*
        retval = PAPI_stop (EventSet, values);
        
        
	if (retval != PAPI_OK) {
		printf("PAP stop failed\n");
		printf("Error: %d\n", retval);
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
	} else {
                printf("PAP stop Success\n");
                printf("pthread: %lu\n", pthread_self());
                int c = sched_getcpu();
                printf("CPU: %d\n", c);
        }
        
	if(values[0] > 0 || values[1] > 0) {
		printf("PAPI STATS - pthreadid: %lu\n", pthread_self());
		printf("Total number of CPU cycles %llu \n", values[0]);
		printf("Total number Instructions completed %llu \n", values[1]);
		// printf("Total number L3 data cache misses %lld \n", values[2]);
		// printf("Total number L3 instruction cache misses %lld \n", values[3]);
	}
        */

        if(t->index == 0) {
                /*
                long long count;

                ioctl(fd_cyc1, PERF_EVENT_IOC_DISABLE, 0);
                read(fd_cyc1, &count, sizeof(count));
                printf("Used %lld cycles on core 1\n", count);
                

                ioctl(fd_instr1, PERF_EVENT_IOC_DISABLE, 0);
                read(fd_instr1, &count, sizeof(count));
                printf("Used %lld instructions on core 1\n", count);

                ioctl(fd_cyc2, PERF_EVENT_IOC_DISABLE, 0);
                read(fd_cyc2, &count, sizeof(count));
                printf("Used %lld cycles on core 2\n", count);
                

                ioctl(fd_instr2, PERF_EVENT_IOC_DISABLE, 0);
                read(fd_instr2, &count, sizeof(count));
                printf("Used %lld instructions on core 2\n", count);

                close(fd_cyc1);
                close(fd_instr1);
                close(fd_cyc2);
                close(fd_instr2);
                */
        }
        ///////////////////////////////////////////////////////////////////////////////////////////////////
        printf("Event Loop completed for thread_id: %d\n", t->index);
        thread_flush_stat(t);
        free(events);

        // TODO: Close tcpqueue
        //do_close(t->epfd);

        /* TODO: The first flow object is leaking here... */

        /* This is technically a thread callback so it must return a (void *) */

        thread_exit();
        // return NULL;
}
