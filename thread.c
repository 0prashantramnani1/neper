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

#include <sched.h>
#include <sys/eventfd.h>

#include "common.h"
#include "control_plane.h"
#include "cpuinfo.h"
#include "flow.h"
#include "histo.h"
#include "loop.h"
#include "percentiles.h"
#include "pq.h"
#include "print.h"
#include "rusage.h"
#include "snaps.h"
#include "stats.h"
#include "thread.h"

#ifndef NO_LIBNUMA
#include <libnuma/numa.h>
#include <libnuma/numaint.h>
#endif

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <runtime/runtime.h>
#include <runtime/thread.h>
//#include <runtime/sync.h>


// max value = 1.0025^8192 = 764278329
// If TIME_RESOLUTION is 0.01 us, max latency in histogram = 7.642783298s
#define NEPER_HISTO_SIZE   8192  /* # of buckets in the histogram */
#define NEPER_HISTO_GROWTH 1.0025 /* bucket growth rate */

/* Callbacks for the neper_stats sumforeach() function. */

static int
fn_count_events(struct neper_stat *stat, void *ptr)
{
        const struct neper_histo *histo = stat->histo(stat);
        return histo->events(histo);
}

static int
fn_count_snaps(struct neper_stat *stat, void *ptr)
{
        const struct neper_snaps *snaps = stat->snaps(stat);
        return snaps->count(snaps);
}

static int
fn_enq(struct neper_stat *stat, void *ptr)
{
        struct neper_pq *pq = ptr;
        pq->enq(pq, stat);
        return 0;
}

void
thread_resize_flows_or_die(struct thread_neper *ts, int flowid)
{
        int new_max = (ts->flow_space + 1) * 2;
        struct flow **new_flows;

        if (new_max <= flowid)
                new_max = flowid + 1;

        new_flows = malloc_or_die (sizeof(struct flow *) * new_max, ts->cb);

        int i;
        for (i = 0; i < ts->flow_space; i++)
                new_flows[i] = ts->flows[i];

        for (; i < new_max; i++)
                new_flows[i] = NULL;

        free(ts->flows);
        ts->flows = new_flows;
        ts->flow_space = new_max;
}

/* Push out any final measurements */
void
thread_flush_stat(struct thread_neper *ts) {
        int i;

        for (i = 0; i < ts->flow_space; i++) {
                struct flow *f = ts->flows[i];
                if (f == NULL)
                        continue;

                struct neper_stat *stat = flow_stat(f);
                if (stat != NULL)
                        stat->event(ts, stat, 0, true, NULL);
        }
}

/* Disassociate flow from thread */
void
thread_clear_flow_or_die(struct thread *ts, struct flow *f)
{
        int flowid = flow_id(f);

        if (flowid >= ts->flow_space) {
                LOG_FATAL(ts->cb, "thread %d freeing unknown flow %d - %p",
                          ts->index, flowid, f);
        }
        if (ts->flows[flowid] != f) {
                LOG_FATAL(ts->cb, "thread %d freeing mismatched flow %d "
                          "- %p vs %p",
                          ts->index, flowid, f, ts->flows[flowid]);
        }

        ts->flows[flowid] = NULL;
}

/* Associate flow with thread */
void
thread_store_flow_or_die(struct thread_neper *ts, struct flow *f) {
        int flowid = flow_id(f);

        if (flowid >= ts->flow_space) {
                thread_resize_flows_or_die(ts, flowid);
        }

        if (ts->flows[flowid] != NULL) {
                LOG_FATAL(ts->cb, "thread %d duplicate flow %d "
                          "- new %p vs old %p", ts->index,
                          flowid, f, ts->flows[flowid]);
        }
        ts->flows[flowid] = f;
}


/* Return the total number of events across all threads.  */

int thread_stats_events(const struct thread *ts)
{
        const struct options *opts = ts[0].opts;
        int i, sum = 0;

        for (i = 0; i < opts->num_threads; i++) {
                struct neper_stats *stats = ts[i].stats;
                sum += stats->sumforeach(stats, fn_count_events, NULL);
        }
        return sum;
}

/* Return the total number of snapshots across all threads. */

int thread_stats_snaps(const struct thread_neper *ts)
{
        const struct options *opts = ts[0].opts;
        int i, sum = 0;

        for (i = 0; i < opts->num_threads; i++) {
                struct neper_stats *stats = ts[i].stats;
                sum += stats->sumforeach(stats, fn_count_snaps, NULL);
        }
        return sum;
}

/*
 * Return the total number of flows for which snapshots have been collected
 * across all threads.
 */

static int thread_stats_flows(const struct thread_neper *ts)
{
        const struct options *opts = ts[0].opts;
        int i, num_flows = 0;

        for (i = 0; i < opts->num_threads; i++)
                num_flows += ts[i].stats->flows(ts[i].stats);
        return num_flows;
}

/*
 * Create a priority queue, fill it with all stats structs across all threads,
 * return it to the caller.
 */

struct neper_pq *thread_stats_pq(struct thread_neper *ts)
{
        const struct options *opts = ts[0].opts;
        struct callbacks *cb = ts[0].cb;
        int i;

        int num_snaps = thread_stats_snaps(ts);
        PRINT(cb, "num_samples", "%d", num_snaps);
        if (num_snaps < 2) {
                LOG_ERROR(cb, "insufficient number of samples, "
                          "needed 2 or more, got %d", num_snaps);
                return NULL;
        }

        struct neper_pq *pq = neper_pq(neper_stat_cmp, thread_stats_flows(ts),
                                       cb);

        for (i = 0; i < opts->num_threads; i++)
                ts[i].stats->sumforeach(ts[i].stats, fn_enq, pq);
        return pq;
}

static int flows_in_thread(const struct thread_neper *t)
{
        const struct options *opts = t->opts;
        const int num_flows = opts->num_flows;
        const int num_threads = opts->num_threads;
        const int tid = t->index;

        const int min_flows_per_thread = num_flows / num_threads;
        const int remaining_flows = num_flows % num_threads;
        const int flows_in_this_thread = tid < remaining_flows ?
                                         min_flows_per_thread + 1 :
                                         min_flows_per_thread;
        return flows_in_this_thread;
}

static int first_flow_in_thread(const struct thread_neper *t)
{
        const struct options *opts = t->opts;
        const int num_flows = opts->num_flows;
        const int num_threads = opts->num_threads;
        const int tid = t->index;

        const int a = num_flows / num_threads;
        const int b = num_flows % num_threads;
        const int c = MIN(b, tid);

        return tid * a + c;
}

/* Fill out cpuset array with allowed CPUs. See get_cpuinfo() for more details.
 * input params: cpuset: array of cpu_set_t to be filled
 *               cb: general callback struct
 * return: number of filled cpu_set_t in array cpuset
 */
static int get_cpuset(cpu_set_t *cpuset, struct callbacks *cb)
{
        int i, j = 0, n;
        struct cpuinfo *cpus;
        cpu_set_t allowed_cpus;
        int len = 0;
        char *allowed_cores;
        int start = -1, end = -1;

        CPU_ZERO(&allowed_cpus);
        if (sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus))
                PLOG_FATAL(cb, "sched_getaffinity");

        cpus = calloc(CPU_SETSIZE, sizeof(struct cpuinfo));
        if (!cpus)
                PLOG_FATAL(cb, "calloc cpus");
        n = get_cpuinfo(cpus, CPU_SETSIZE, cb);
        if (n == -1)
                PLOG_FATAL(cb, "get_cpuinfo");
        if (n == 0)
                LOG_FATAL(cb, "no cpu found in /proc/cpuinfo");

        /* Assume each processor ID takes max 3 chars + ','
         * Last one does not have ',' so we have room for '\0'
         */
        allowed_cores = calloc(n, 4);
        if (!allowed_cores)
                PLOG_FATAL(cb, "calloc allowed_cores");

        for (i = 0; i < n; i++) {
                if (CPU_ISSET(cpus[i].processor, &allowed_cpus)) {
                        CPU_ZERO(&cpuset[j]);
                        CPU_SET(cpus[i].processor, &cpuset[j]);
                        j++;

                        if (start < 0)
                                start = end = i;
                        else if (i == end + 1)
                                end = i;
                }

                if (start >= 0 && (i != end || i == n - 1)) {
                        len += sprintf(allowed_cores + len,
                                       end == start ? "%s%d" : "%s%d-%d",
                                       len ? "," : "", start, end);
                        start = -1;
                }
        }
        PRINT(cb, "allowed_core_num", "%d", j);
        PRINT(cb, "allowed_cores", "%s", allowed_cores);
        free(allowed_cores);
        free(cpus);
        return j;
}

void thread_time_start(struct thread_neper *t, const struct timespec *now)
{
        mutex_lock(t->time_start_mutex);

        if (timespec_is_zero(t->time_start)) {
                LOG_INFO(t->cb, "Setting time_start in thread %d", t->index);
                getrusage_enhanced(RUSAGE_SELF, t->rusage_start);
                *t->time_start = *now;
        }

        mutex_unlock(t->time_start_mutex);
}

#ifndef NO_LIBNUMA
static void get_numa_allowed_cpus(struct callbacks *cb, int numa_idx,
                                  cpu_set_t *numa_allowed_cpus)
{
        cpu_set_t allowed_cpus;
        cpu_set_t numa_cpus;
        int i;

        CPU_ZERO(&allowed_cpus);
        if (sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus))
                PLOG_FATAL(cb, "sched_getaffinity");

        CPU_ZERO(&numa_cpus);
        for (i = 0; i < numa_num_configured_cpus(); i++) {
                if (numa_node_of_cpu(i) == numa_idx)
                        CPU_SET(i, &numa_cpus);
        }

        CPU_ZERO(numa_allowed_cpus);
        CPU_AND(numa_allowed_cpus, &allowed_cpus, &numa_cpus);
}
#endif

void start_worker_threads(struct options *opts, struct callbacks *cb,
                          struct thread_neper *t, void *(*thread_func)(void *),
                          const struct neper_fn *fn,
                          barrier_t *ready, barrier_t *finish, 
                          barrier_t* papi_start, barrier_t* papi_end, struct timespec *time_start,
                          pthread_mutex_t *time_start_mutex,
                          struct rusage *rusage_start, struct addrinfo *ai,
                          struct countdown_cond *data_pending,
                          pthread_cond_t *loop_init_c,
                          pthread_mutex_t *loop_init_m,
                          int *loop_inited)
{
	LOG_INFO(cb, "Configuring woker threads \n");
        printf("Configuring woker threads \n");
        
        int s, i, allowed_cores;

	barrier_init(ready, opts->num_threads + 1);

        /// PAPI
        barrier_init(papi_start, opts->num_threads);
        barrier_init(papi_end, opts->num_threads);
        
        int percentiles = percentiles_count(&opts->percentiles);

        for (i = 0; i < opts->num_threads; i++) {
                t[i].index = i;
                t[i].fn = fn;
                create_waiter(&t[i].waiter);
                create_trigger(&t[i].stop_trigger);
                t[i].opts = opts;
                t[i].cb = cb;
                t[i].num_local_hosts = count_local_hosts(opts);
                t[i].flow_first = first_flow_in_thread(&t[i]);
                t[i].flow_limit = flows_in_thread(&t[i]);
                t[i].flow_count = 0;
                t[i].percentiles = percentiles;
                // TODO: LOCAL HOSTS
                //t[i].local_hosts = parse_local_hosts(opts, t[i].num_local_hosts,
                //                                     cb);
                t[i].ready = ready;
                t[i].finish = finish;
                
                // PAPI
                t[i].papi_start = papi_start;
                t[i].papi_end = papi_end;

                t[i].time_start = time_start;
                t[i].time_start_mutex = time_start_mutex;
                t[i].rusage_start = rusage_start;
                t[i].stats = neper_stats_init(cb);
                t[i].rusage = neper_rusage(opts->interval);
                // TODO: DATA PENDING
                //t[i].data_pending = data_pending;
                t[i].histo_factory = neper_histo_factory(&t[i],
                                                        NEPER_HISTO_SIZE,
                                                        NEPER_HISTO_GROWTH);
                t[i].loop_inited = loop_inited;
                t[i].loop_init_c = loop_init_c;
                t[i].loop_init_m = loop_init_m;


                t[i].flows = NULL;
                t[i].flow_space = 0;
		
                /* support for rate limited flows */
	
                //t[i].rl.pending_flows = calloc_or_die(t[i].flow_limit,
                //                              sizeof(struct flow *), t->cb);
                //t[i].rl.pending_count = 0;
                //t[i].rl.next_event = ~0ULL;

                //printf("Main thread spawning uthread%d on pthreadid %d - kthreadid %d\n", i+1, syscall(__NR_gettid), get_current_affinity());
                s = thread_spawn(thread_func, &t[i]);
                
                if (s != 0)
                       LOG_FATAL(cb, "thread_spawn: %s", strerror(s));
		
        }
	

        printf("Waiting for server threads to setup\n");
        barrier_wait(ready);
        printf("worker threads are ready\n");
        LOG_INFO(cb, "worker threads are ready");
	
}

void stop_worker_threads(struct callbacks *cb, int num_threads,
                         struct thread_neper *t, pthread_barrier_t *ready,
                         pthread_cond_t *loop_init_c,
                         pthread_mutex_t *loop_init_m)
{
        int i, s;
        uint64_t total_sleep = 0, total_delay = 0, total_reschedule = 0;

        /* tell them to stop */
        for (i = 0; i < num_threads; i++) {
                poll_trigger(t[i].waiter, t[i].stop_trigger);
                // if (eventfd_write(t[i].stop_efd, 1))
                //         PLOG_FATAL(cb, "eventfd_write");
                // else
                //         LOG_INFO(cb, "told thread %d to stop", i);
        }

        /* wait for them to stop */
        // for (i = 0; i < num_threads; i++) {
        //         s = pthread_join(t[i].id, NULL);
        //         if (s != 0)
        //                 LOG_FATAL(cb, "pthread_join: %s", strerror(s));
        //         else
        //                 LOG_INFO(cb, "joined thread %d", i);
        //         total_delay += t[i].rl.delay_count;
        //         total_sleep += t[i].rl.sleep_count;
        //         total_reschedule += t[i].rl.reschedule_count;
        // }
        int a = -1;
        while(a == -1)
               a = system("sudo kill -SIGINT `pgrep perf`");

        printf("ALl event loops stopped \n");
        uint64_t stop_us = microtime() + 175 * ONE_MS;
        while (microtime() < stop_us);
        

        unsigned long long int total_data_sent = 0;
        for (i = 0; i < num_threads; i++) {
                total_data_sent += t[i].total_reqs;
        }

        printf("Total data send: %llu\n", total_data_sent);
        // LOG_INFO(cb, "reschedule=%lu", total_reschedule);
        // LOG_INFO(cb, "delay=%lu", total_delay);
        // LOG_INFO(cb, "sleep=%lu", total_sleep);
        // s = pthread_barrier_destroy(ready);
        // if (s != 0)
        //         LOG_FATAL(cb, "pthread_barrier_destroy: %s", strerror(s));

        // s = pthread_cond_destroy(loop_init_c);
        // if (s != 0)
        //         LOG_FATAL(cb, "pthread_cond_destroy: %s", strerror(s));

        // s = pthread_mutex_destroy(loop_init_m);
        // if (s != 0)
        //         LOG_FATAL(cb, "pthread_mutex_destroy: %s", strerror(s));
}

static void free_worker_threads(int num_threads, struct thread *t)
{
        int i;

        for (i = 0; i < num_threads; i++) {
                do_close(t[i].stop_efd);
                free(t[i].ai);
                t[i].rusage->fini(t[i].rusage);
                free(t[i].rl.pending_flows);
                free(t[i].f_mbuf);
                free(t[i].flows);
        }
        free(t);
}

int run_main_thread(struct options *opts, struct callbacks *cb,
                    const struct neper_fn *fn)
{
        void *(*thread_func)(void *) = (void *)loop; /* shared by threads */

        //CALADAN
	barrier_t ready_barrier;
        barrier_t finish_barrier;
        barrier_init(&finish_barrier, opts->num_threads + 1);

	tcpqueue_t *control_plane_q;

        barrier_t papi_start_barrier;
        barrier_t papi_end_barrier;
        
        struct timespec time_start = {0}; /* shared by flows */

        //CALADAN
	mutex_t time_start_mutex;
	mutex_init(&time_start_mutex);

        // TODO: Check
        struct rusage rusage_start; /* updated when first packet comes */
        struct rusage rusage_end; /* local to this function, never pass out */

        struct addrinfo *ai;
        struct thread_neper *ts; /* worker threads */
        struct control_plane *cp;

        // TODO: Check
        struct countdown_cond *data_pending;

        /* Set the options used for capturing cpu usage */
	/*
        if (opts->stime_use_proc)
                set_getrusage_enhanced(opts->stime_use_proc, opts->num_threads);

        if (opts->delay)
                prctl(PR_SET_TIMERSLACK, 1UL);
	*/
        
        
	condvar_t loop_init_c;
	condvar_init(&loop_init_c);

	mutex_t loop_init_m;
	mutex_init(&loop_init_m);

        int loop_inited = 0;

        if (opts->test_length > 0) {
                PRINT(cb, "total_run_time", "%d", opts->test_length);
                data_pending = NULL;
        } 
        if(opts->data_pending > 0) {
                opts->test_length = 0;
        }

        if (opts->dry_run)
                return 0;

        cp = control_plane_create(opts, cb, data_pending, fn);
        control_plane_start(cp, &ai, control_plane_q);
        printf("Started control plane\n");

        /* start threads *after* control plane is up, to reuse addrinfo. */
        ts = calloc(opts->num_threads, sizeof(struct thread_neper));
        start_worker_threads(opts, cb, ts, thread_func, fn,  &ready_barrier, &finish_barrier, 
                             &papi_start_barrier, &papi_end_barrier, 
                             &time_start, &time_start_mutex, &rusage_start, ai,
                             data_pending, &loop_init_c,
                             &loop_init_m, &loop_inited);
			     
        LOG_INFO(cb, "started worker threads");

        /* rusage_start is now exposed to other threads  */
        mutex_lock(&time_start_mutex);
        getrusage_enhanced(RUSAGE_SELF, &rusage_start); /* rusage start! */
        mutex_unlock(&time_start_mutex);
        control_plane_wait_until_done(cp);

        getrusage_enhanced(RUSAGE_SELF, &rusage_end); /* rusage end! */
        printf("Received Notif from client, going to stop worker threads now\n");
        
        stop_worker_threads(cb, opts->num_threads, ts, &ready_barrier,
                           &loop_init_c, &loop_init_m);
        LOG_INFO(cb, "stopped worker threads");

        PRINT(cb, "invalid_secret_count", "%d", control_plane_incidents(cp));
	
	
        /* rusage_start and time_start were (are?) visible to other threads */
        mutex_lock(&time_start_mutex);
        /* begin printing rusage */
	
        PRINT(cb, "time_start", "%ld.%09ld", time_start.tv_sec,
              time_start.tv_nsec);
        PRINT(cb, "utime_start", "%ld.%06ld", rusage_start.ru_utime.tv_sec,
              rusage_start.ru_utime.tv_usec);
        PRINT(cb, "utime_end", "%ld.%06ld", rusage_end.ru_utime.tv_sec,
              rusage_end.ru_utime.tv_usec);
        PRINT(cb, "stime_start", "%ld.%06ld", rusage_start.ru_stime.tv_sec,
              rusage_start.ru_stime.tv_usec);
        PRINT(cb, "stime_end", "%ld.%06ld", rusage_end.ru_stime.tv_sec,
              rusage_end.ru_stime.tv_usec);
        PRINT(cb, "maxrss_start", "%ld", rusage_start.ru_maxrss);
        PRINT(cb, "maxrss_end", "%ld", rusage_end.ru_maxrss);
        PRINT(cb, "minflt_start", "%ld", rusage_start.ru_minflt);
        PRINT(cb, "minflt_end", "%ld", rusage_end.ru_minflt);
        PRINT(cb, "majflt_start", "%ld", rusage_start.ru_majflt);
        PRINT(cb, "majflt_end", "%ld", rusage_end.ru_majflt);
        PRINT(cb, "nvcsw_start", "%ld", rusage_start.ru_nvcsw);
        PRINT(cb, "nvcsw_end", "%ld", rusage_end.ru_nvcsw);
        PRINT(cb, "nivcsw_start", "%ld", rusage_start.ru_nivcsw);
        PRINT(cb, "nivcsw_end", "%ld", rusage_end.ru_nivcsw);
	
        mutex_unlock(&time_start_mutex);
        /* end printing rusage */
        // barrier_wait(&finish_barrier);

        int ret = fn->fn_report(ts);
	printf("WORKING 2 \n");
        control_plane_stop_linux(cp);
        //control_plane_destroy(cp);
        PRINT(cb, "local_throughput", "%lld", opts->local_rate);
        PRINT(cb, "remote_throughput", "%lld", opts->remote_rate);

        //free_worker_threads(opts->num_threads, ts);
        //free(data_pending);
        return ret;
}
