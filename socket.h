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

#ifndef THIRD_PARTY_NEPER_SOCKET_H
#define THIRD_PARTY_NEPER_SOCKET_H

// CALADAN
#include<runtime/tcp.h>

struct thread;
struct thread_neper;

void socket_listen(struct thread_neper *);

tcpconn_t  *socket_connect_one(struct thread_neper *, int flags);
void socket_connect_all(struct thread_neper *);

#endif
