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

#ifndef THIRD_PARTY_NEPER_STREAM_H
#define THIRD_PARTY_NEPER_STREAM_H

#include <stdint.h>

// CALADAN
#include <runtime/tcp.h>

struct flow;
struct thread;
struct thread_neper;

//void stream_flow_init(struct thread *, int fd);
void stream_flow_init(struct thread_neper *, tcpconn_t *);
void stream_handler(struct flow *, uint32_t events);
int stream_report(struct thread_neper *);

#endif
