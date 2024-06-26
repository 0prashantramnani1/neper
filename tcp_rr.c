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
#include "rr.h"
#include "socket.h"
#include "thread.h"

static const struct neper_fn client_fn = {
        .fn_loop_init = socket_connect_all,
        .fn_flow_init = rr_flow_init,
        .fn_report    = rr_report_stats,
        .fn_type      = SOCK_STREAM
};

static const struct neper_fn server_fn = {
        .fn_loop_init = socket_listen,
        .fn_flow_init = rr_flow_init,
        .fn_report    = rr_report_stats,
        .fn_type      = SOCK_STREAM
};

//int tcp_rr(struct options *opts, struct callbacks *cb)
int tcp_rr(struct arg_struct *arg)
{
	struct options *opts = arg->opts;
	struct callbacks *cb = arg->cb;
        const struct neper_fn *fn = opts->client ? &client_fn : &server_fn;
        return run_main_thread(opts, cb, fn);
}
