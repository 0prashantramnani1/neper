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

#include "stream.h"

#include "coef.h"
#include "common.h"
#include "flow.h"
#include "print.h"
#include "socket.h"
#include "stats.h"
#include "thread.h"

#include <base/stddef.h>

static void *stream_alloc(struct thread_neper *t)
{
        const struct options *opts = t->opts;
        
        if(opts->private_buffers) {
                void* f_mbuf = malloc_or_die(opts->buffer_size, t->cb);
                if (opts->enable_write)
                        fill_random(f_mbuf, opts->buffer_size);
                return f_mbuf;
        }

        if (!t->f_mbuf) {
                t->f_mbuf = malloc_or_die(opts->buffer_size, t->cb);
                if (opts->enable_write)
                        fill_random(t->f_mbuf, opts->buffer_size);
        }
        return t->f_mbuf;
}

static uint32_t stream_events(struct thread_neper *t)
{
        const struct options *opts = t->opts;

        uint32_t events = 0; //EPOLLRDHUP;
        if (opts->enable_write)
                events |= SEV_WRITE;
                // events |= EPOLLOUT;
        if (opts->enable_read)
                events |= SEV_READ;
                // events |= EPOLLIN;
        // TODO: Implement Edge trigger properly                
        // if (opts->edge_trigger)
        //         events |= EPOLLET;
        return events;
}

void stream_handler(struct flow *f, uint32_t events)
{
        static const uint64_t NSEC_PER_SEC = 1000*1000*1000;

        struct neper_stat *stat = flow_stat(f);
        struct thread_neper *t = flow_thread(f);
        void *mbuf = flow_mbuf(f);
        int* offset = flow_data_offset(f);
        static bool main_started = false;

        // int fd = flow_fd(f);
        tcpconn_t *c = flow_connection(f);
        // printf("size: %d\n", sizeof(*c));
        const struct options *opts = t->opts;
        long long int data_pending = (opts->data_pending + opts->num_threads - 1)/opts->num_threads;

        if(t->index == 1) {
                // printf("IN STREAM HANDLE UTH\n");
        }
        /*
         * The actual size can be calculated with CMSG_SPACE(sizeof(struct X)),
         * where X is unnamed structs defined in kernel source tree based on IP versions.
         *      net/ipv4/ip_sockglue.c:ip_recv_error()
         *      net/ipv6/datagram.c:ipv6_recv_error()
         * For IPv6, it's
         *      struct {
         *              struct sock_extended_err ee;            // 16
         *              struct sockaddr_in6      offender;      // 28
         *      } errhdr;
         * As of Linux 5.15, CMSG_SPACE() is 16 + 16 + 28, rounds up to 64.
         * Choosing 128 should last for a while.
         */
        char control[128];
        struct msghdr msg = {
                .msg_control = control,
                .msg_controllen = sizeof(control),
        };
        ssize_t n;

        // TODO: Delete flows
        // if (events & (EPOLLHUP | EPOLLRDHUP))
        //         return flow_delete(f);

        if (events & SEV_READ) {
                n = tcp_read(c, mbuf, opts->buffer_size);
                tcpconn_check_triggers(c);
                if (n < 0) {
                        // if (errno != EAGAIN)
                        // PLOG_ERROR(t->cb, "read");
                        // break;
                }

                stat->event(t, stat, n, false, NULL);
                t->total_reqs += n;

                ///////// CHECK ///////////
                // struct timespec now;
                // common_gettime(&now);
                // t->time_buckets[(int)seconds_between(t->time_start, &now)] += n;
                // t->time_buckets[t->total_reqs/10000000] += n;

        }
        int k = 1;

        if (events & SEV_WRITE)
                do {
                        n = tcp_write(c, mbuf + *offset, MIN(opts->batch_size, opts->buffer_size - *offset));
                        if(n > 0) {
                                *offset = (*offset) + n;
                                if(*offset >= opts->buffer_size) {
                                        *offset = 0;
                                }

                                t->total_reqs += n;
                                if(opts->data_pending > 0 && t->total_reqs > (data_pending * (1e9))) {
                                        barrier_wait(t->data_pending_barrier);
                                        if(!main_started && __u_main != NULL && t->index == 0) {
                                                main_started = true;
                                                thread_ready(__u_main);
                                                // preempt_disable();
                                                // thread_park_and_preempt_enable();
                                        }
                                }

                                t->succ_write_calls++;
                                t->succ_before_yield++;
                                if(n < 16384) {
                                        t->volunteer_yields++;
                                        // if(t->index == 0) 
                                        thread_yield();
                                        // if(t->index == 1)
                                                // thread_yield_without_ready();
                                }
                        } else if(n == -ENOBUFS) { // No space left
                                if(t->succ_before_yield == 0)
                                        t->no_work_schedule++;
                                t->succ_before_yield = 0;
                                // t->volunteer_yields = 0;
                                t->volunteer_yields++;
                                // if(t->index == 0) 
                                thread_yield();
                                // if(t->index == 1)
                                        // thread_yield_without_ready();
                        }
                        // else if(t->index == 1 && softirq_run()) {
                                // if(t->index == 0) 
                                        // thread_yield();
                                // thread_yield_without_ready();
                                // t->blocked_calls++;
                                // if(t->blocked_calls >= 50) {
                                //         t->blocked_calls = 0;
                                //         thread_yield();
                                // }
                        // }

                        if (opts->delay) {
                                struct timespec ts;
                                ts.tv_sec  = opts->delay / NSEC_PER_SEC;
                                ts.tv_nsec = opts->delay % NSEC_PER_SEC;
                                nanosleep(&ts, NULL);
                        }
                        // if(n == -105) {
                        //         printf("Failed to write for flow: %d\n", flow_id(f));
                        // }
                } while(--k);//while (opts->edge_trigger);

        //TODO: Look at error
        // if (events & EPOLLERR) {
        //         do {
        //                 n = recvmsg(fd, &msg, MSG_ERRQUEUE);
        //         } while(n == -1 && errno == EINTR);
        //         if (n == -1) {
        //                 if (errno != EAGAIN)
        //                         PLOG_ERROR(t->cb, "recvmsg() on ERRQUEUE failed");
        //                 return;
        //         }
        //         /*
        //          * No need to process anything for the purpose of benchmarking,
        //          * as flow_mbuf(f) won't be released before flow is terminated.
        //          *
        //          * Maybe examine sock_extended_err.ee_code to find out whether
        //          * zerocopy actually happened. i.e. SO_EE_CODE_ZEROCOPY_COPIED
        //          * e.g. Linux kernel tools/testing/selftests/net/msg_zerocopy.c
        //          */
        // }
}

int stream_report(struct thread_neper *ts)
{
        const struct options *opts = ts[0].opts;
        const char *path = opts->all_samples;
        struct callbacks *cb = ts[0].cb;
        FILE *csv = NULL;

        if (!opts->enable_read)
                return 0;

        if (path)
                csv = print_header(path, "bytes_read,bytes_read/s", "\n", cb);

        struct neper_coef *coef = neper_stat_print(ts, csv, NULL);
        if (!coef) {
                LOG_ERROR(ts->cb, "%s: not able to find coef", __func__);
                return -1;
        }

        const struct rate_conversion *units = opts->throughput_opt;
        if (units) {
                double thru = coef->thruput(coef);
                /* This is only run by the control thread */
                struct options *w_opts = (struct options *)opts;
                w_opts->local_rate = 8*thru; /* bits/s */
                if (!units->unit)
                        units = auto_unit(thru, units, cb);
                thru /= units->bytes_per_second;
                PRINT(cb, "throughput", "%.2f", thru);
                PRINT(cb, "throughput_units", "%s", units->unit);
        }

        if (csv)
                fclose(csv);
        coef->fini(coef);

        return 0;
}

static struct neper_stat *neper_stream_init(struct flow *f)
{
        return neper_stat_init(f, NULL, 0);
}

void stream_flow_init(struct thread_neper *t, tcpconn_t *c)
{       
        const struct flow_create_args args = {
                .thread  = t,
                // .fd      = fd,
                .q       = NULL,
                .c       = c,
                .events  = stream_events(t),
                .opaque  = NULL,
                .handler = stream_handler,
                // TODO: Calculate Stats
                .stat    = neper_stream_init,
                .mbuf_alloc = stream_alloc
        };

        flow_create(&args);
}
