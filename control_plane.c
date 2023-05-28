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

#include "control_plane.h"
#include <netinet/tcp.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include "countdown_cond.h"
#include "hexdump.h"
#include "lib.h"
#include "logging.h"

// CALADAN
#include <runtime/tcp.h>
#include <net/ip.h>


/*
 * Client and server exchange typed (struct hs_msg) on the control
 * connection to synchronize and pass parameters and results.
 * The handshake is as follows:
 *   CLI --> SER : CLI_HELLO  with client command line arguments
 *   CLI <-- SER : SER_ACK    with server command line arguments
 *        <data transfer>
 *   CLI --> SER : CLI_DONE  with client results if any
 *   CLI <-- SER : SER_BYE   with server results if any
 * The first message includes a test name and version, plus a user specified
 * command number.  Numeric fields are in network format.
 */

enum msg_type { CLI_HELLO = 1, SER_ACK, CLI_DONE, SER_BYE};
const char *msg_types[] = {"--", "CLI_HELLO", "SER_ACK", "CLI_DONE", "CLI_BYE"};
struct hs_msg {
        char secret[32];           /* test name and version number */
        int32_t magic;
        uint32_t type;
        uint32_t num_threads;
        uint32_t num_flows;        /* Client only */
        uint32_t test_length;      /* Client only */
        uint32_t client_number;    /* Server only */
        uint64_t max_pacing_rate;  /* Client only */
        uint64_t remote_rate;      /* bits/s or trans/s */
};

static int recv_msg(int fd, struct hs_msg *msg, struct callbacks *cb,
                    const char *fn)
{
        int n, magic = ntohl(msg->magic), type = ntohl(msg->type);
        static const char *dbg[] = {"unknown type",
                "SER <-- CLI   CLI_HELLO", "CLI <-- SER   SER_ACK",
                "SER <-- CLI   CLI_DONE", "CLI <-- SER   CLI_BYE"};

        LOG_INFO(cb, "--- %s (%d)", dbg[ type <= SER_BYE ? type : 0 ], type);
                memset(msg, 0, sizeof(*msg));
        while ((n = read(fd, msg, sizeof(*msg))) == -1) {
                if (errno == EINTR || errno == EAGAIN)
                        continue;
                PLOG_FATAL(cb, "%s: read", fn);
        }
        if (n == sizeof(*msg) && magic == ntohl(msg->magic) && type == ntohl(msg->type))
                return 0;  /* all good. */
        LOG_ERROR(cb, "%s: Read error: read want %lu bytes, magic %d type %d,"
                 " have %d %d %d", fn, sizeof(*msg), magic, type,
                n, ntohl(msg->magic), ntohl(msg->type));
        return 1;  /* error */
}

static void send_msg(int fd, struct hs_msg *msg, struct callbacks *cb,
                     const char *fn)
{
        int n;

        while ((n = write(fd, msg, sizeof(*msg))) == -1) {
                if (errno == EINTR || errno == EAGAIN)
                        continue;
                PLOG_FATAL(cb, "%s: write", fn);
        }
        if (n != sizeof(*msg))
                LOG_FATAL(cb, "%s: Incomplete write %d", fn, n);
}

// CALADAN
static void send_msg_caladan(tcpconn_t *c, struct hs_msg *msg, struct callbacks *cb,
                     const char *fn)
{
        int n;

	printf("2.1\n");
        // while ((n = write(fd, msg, sizeof(*msg))) == -1) {
	printf("SIZE OF MSG: %d\n", sizeof(*msg));
        while (n = tcp_write(c, msg, sizeof(*msg)) == -1) {                
		printf("2.2\n");
                if (errno == EINTR || errno == EAGAIN)
                        continue;
                PLOG_FATAL(cb, "%s: write", fn);
        }
	printf("2.3\n");
        //if (n != sizeof(*msg))
        //        LOG_FATAL(cb, "%s: Incomplete write %d", fn, n);
}
//

static int try_connect(int s, const struct sockaddr *addr, socklen_t addr_len)
{
        for (;;) {
                int ret = connect(s, addr, addr_len);
                if (ret == -1 && (errno == EINTR || errno == EALREADY))
                        continue;
                if (ret == -1 && errno == EISCONN)
                        return 0;
                return ret;
        }
}

static int connect_any(const char *host, const char *port, struct addrinfo **ai,
                       struct options *opts, struct callbacks *cb)
{
        struct addrinfo *result, *rp;
        int sfd = -1, num_local_hosts, allowed_retry = 30;

        num_local_hosts = count_local_hosts(opts);
        struct addrinfo** local_hosts =
                parse_local_hosts(opts, num_local_hosts, cb);

        const struct addrinfo hints = {
                .ai_flags    = 0,
                .ai_family   = get_family(opts),
                .ai_socktype = SOCK_STREAM
        };

        result = getaddrinfo_or_die(host, port, &hints, cb);
retry:
        /* getaddrinfo() returns a list of address structures.
         * Try each address until we successfully connect().
         */
        for (rp = result; rp != NULL; rp = rp->ai_next) {
                sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (sfd == -1) {
                        if (errno == EMFILE || errno == ENFILE ||
                            errno == ENOBUFS || errno == ENOMEM)
                                PLOG_FATAL(cb, "socket");
                        /* Other errno's are not fatal. */
                        PLOG_ERROR(cb, "socket");
                        continue;
                }
                if (opts->freebind)
                        set_freebind(sfd, cb);
                /* Bind control socket to first local host if provided. */
                if (local_hosts)
                        bind_or_die(sfd, local_hosts[0], cb);
                if (try_connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
                        break;
                PLOG_ERROR(cb, "connect");
                do_close(sfd);
        }
        if (rp == NULL) {
                if (allowed_retry-- > 0) {
                        sleep(1);
                        goto retry;
                }
                LOG_FATAL(cb, "Could not connect");
        }
        *ai = copy_addrinfo(rp);
        freeaddrinfo(result);
        return sfd;
}

// CALADAN
// TODO: Move to a better location
// static int str_to_ip(const char *str, uint32_t *addr)
// {
// 	uint8_t a, b, c, d;
// 	if(sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
// 		return -EINVAL;
// 	}

// 	*addr = MAKE_IP_ADDR(a, b, c, d);
// 	return 0;
// }

static tcpconn_t *ctrl_connect(const char *host, const char *port,
                        struct addrinfo **ai, struct options *opts,
                        struct callbacks *cb)
{
        int ctrl_conn, optval = 1;
        struct hs_msg msg = {};
        
        msg = (struct hs_msg){ .magic = htonl(opts->magic),
                .type = htonl(CLI_HELLO),
                .num_threads = htonl(opts->num_threads),
                .num_flows = htonl(opts->num_flows),
                .test_length = htonl(opts->test_length),
                .max_pacing_rate = htobe64(opts->max_pacing_rate),
        };

        // CALADAN
        static struct netaddr raddr, laddr;
        uint32_t addr;
        int ret = str_to_ip(host, &addr);

        laddr.ip = 0;
	laddr.port = 0;

        raddr.ip = addr;
        raddr.port = (uint16_t)atoi(port);

        tcpconn_t *c;
        ret = tcp_dial(laddr, raddr, &c);
        
        memcpy(msg.secret, opts->secret, sizeof(msg.secret));
        LOG_INFO(cb, "+++ CLI --> SER   CLI_HELLO -T %d -F %d -l %d -m %" PRIu64,
                 ntohl(msg.num_threads), ntohl(msg.num_flows),
                 ntohl(msg.test_length), be64toh(msg.max_pacing_rate));
        
        send_msg_caladan(c, &msg, cb, __func__);
        printf("Sent control message to Server\n");

        /* Wait for the server to respond */
        msg.type = htonl(SER_ACK);
        
	int len = 0;
	len = tcp_read(c, &msg, sizeof(msg));
        printf("Received control message from Server\n");

	if(len <= 0)
                LOG_FATAL(cb, "exiting");
	printf("Size of the message read from server: %d\n", len);
        LOG_INFO(cb, "+++ CLI <-- SER   SER_ACK -T %d -F %d -l %d -m %" PRIu64,
                 ntohl(msg.num_threads), ntohl(msg.num_flows),
                 ntohl(msg.test_length), be64toh(msg.max_pacing_rate));
        return c;
}

// CALADAN
static int ctrl_listen(const char *host, const char *port,
                       struct addrinfo **ai, struct options *opts,
                       struct callbacks *cb, tcpqueue_t** control_plane_q)
{
	struct netaddr laddr;
	laddr.ip = 0;
	laddr.port = (uint16_t)atoi(port);	
	int ret = tcp_listen(laddr, 4096, control_plane_q);
	if(ret != 0) {
		LOG_ERROR(cb, "Server listen on control_port failed! \n");
	} else {
                printf("Control Plane listening on port: %d\n", laddr.port);
        }
	return ret;
}

////////////////////////////////////////////////////
static int ctrl_listen_linux(const char *host, const char *port,
                       struct addrinfo **ai, struct options *opts,
                       struct callbacks *cb)
{
        struct addrinfo *result, *rp;
        int fd_listen = 0;
        
        const struct addrinfo hints = {
                .ai_flags    = AI_PASSIVE,
                .ai_family   = get_family(opts),
                .ai_socktype = SOCK_STREAM
        };

        result = getaddrinfo_or_die(host, port, &hints, cb);
        for (rp = result; rp != NULL; rp = rp->ai_next) {
                fd_listen = socket(rp->ai_family, rp->ai_socktype,
                                   rp->ai_protocol);
                if (fd_listen == -1) {
                        PLOG_ERROR(cb, "socket");
                        continue;
                }
                set_reuseport(fd_listen, cb);
                set_reuseaddr(fd_listen, 1, cb);
                if (opts->freebind)
                        set_freebind(fd_listen, cb);
                if (bind(fd_listen, rp->ai_addr, rp->ai_addrlen) == 0)
                        break;
                PLOG_ERROR(cb, "bind");
                do_close(fd_listen);
        }
        if (rp == NULL)
                LOG_FATAL(cb, "Could not bind");
        *ai = copy_addrinfo(rp);
        freeaddrinfo(result);
        if (listen(fd_listen, opts->listen_backlog))
                PLOG_FATAL(cb, "listen");
        return fd_listen;
}

static tcpconn_t *ctrl_accept(int ctrl_port, int *num_incidents, struct callbacks *cb,
                       struct options *opts, tcpqueue_t *ctrl_queue)
{
        // TODO: check for exit conditions
        printf("2.1!!!!!!!!\n");
        char dump[8192], host[NI_MAXHOST], port[NI_MAXSERV];
        // struct sockaddr_storage cli_addr;
        // socklen_t cli_len;
        int ctrl_conn = -11, s;

        //CALADAN
        tcpconn_t *ctrl_conn_caladan;
        int ret;
        
        ssize_t len;
        struct hs_msg msg = {};
        printf("2.2!!!!!!!!\n");
retry:
        // cli_len = sizeof(cli_addr);
        while ((ret = tcp_accept(ctrl_queue, &ctrl_conn_caladan)) != 0) {
                // if (errno == EINTR || errno == ECONNABORTED)
                //         continue;
                PLOG_FATAL(cb, "accept");
        }
        printf("2.3!!!!!!!!\n");
        // s = getnameinfo((struct sockaddr *)&cli_addr, cli_len,
        //                 host, sizeof(host), port, sizeof(port),
        //                 NI_NUMERICHOST | NI_NUMERICSERV);
        // if (s) {
        //         LOG_ERROR(cb, "getnameinfo: %s", gai_strerror(s));
        //         strcpy(host, "(unknown)");
        //         strcpy(port, "(unknown)");
        // }
        memset(&msg, 0, sizeof(msg));
        LOG_INFO(cb, "+++ SER <-- CLI ? CLI_HELLO");
        while ((len = tcp_read(ctrl_conn_caladan, &msg, sizeof(msg))) <= 0) {
                // if (errno == EINTR)
                //         continue;
                PLOG_ERROR(cb, "read");
                exit(-1);
                // do_close(ctrl_conn);
                // goto retry;
        }
        printf("2.4!!!!!!!!\n");
        if (memcmp(msg.secret, opts->secret, sizeof(msg.secret)) != 0 ||
            ntohl(msg.type) != CLI_HELLO) {
                if (num_incidents)
                        (*num_incidents)++;
                if (hexdump((void *)&msg, len, dump, sizeof(dump))) {
                        LOG_WARN(cb, "Invalid secret from %s:%s\n%s", host,
                                 port, dump);
                } else
                        LOG_WARN(cb, "Invalid secret from %s:%s", host, port);
                exit(-1);        
                // do_close(ctrl_conn);
                // goto retry;
        }
        printf("2.5!!!!!!!!\n");
        LOG_INFO(cb, "+++ SER <-- CLI   CLI_HELLO -T %d -F %d -l %d -m %" PRIu64,
                 ntohl(msg.num_threads), ntohl(msg.num_flows),
                 ntohl(msg.test_length), be64toh(msg.max_pacing_rate));
        /* tell client that authentication passes */
        msg = (struct hs_msg){ .magic = htonl(opts->magic),
                .type = htonl(SER_ACK),
                .num_threads = htonl(opts->num_threads),
                .num_flows = htonl(opts->num_flows),
                .test_length = htonl(opts->test_length),
        };
        printf("2.6!!!!!!!!\n");
        LOG_INFO(cb, "+++ SER --> CLI   SER_ACK -T %d -F %d -l %d",
                        ntohl(msg.num_threads), ntohl(msg.num_flows),
                        ntohl(msg.test_length));
        // send_msg(ctrl_conn, &msg, cb, __func__);
        send_msg_caladan(ctrl_conn_caladan, &msg, cb, __func__);
        printf("2.7!!!!!!!!\n");
        LOG_INFO(cb, "Control connection established with %s:%s", host, port);
        return ctrl_conn_caladan;
}
////////////////////////////////////////////////////////////////
static int ctrl_accept_linux(int ctrl_port, int *num_incidents, struct callbacks *cb,
                       struct options *opts)
{
        char dump[8192], host[NI_MAXHOST], port[NI_MAXSERV];
        struct sockaddr_storage cli_addr;
        socklen_t cli_len;
        int ctrl_conn, s;
        ssize_t len;
        struct hs_msg msg = {};

retry:
        cli_len = sizeof(cli_addr);
        while ((ctrl_conn = accept(ctrl_port, (struct sockaddr *)&cli_addr,
                                   &cli_len)) == -1) {
                if (errno == EINTR || errno == ECONNABORTED)
                        continue;
                PLOG_FATAL(cb, "accept");
        }
        s = getnameinfo((struct sockaddr *)&cli_addr, cli_len,
                        host, sizeof(host), port, sizeof(port),
                        NI_NUMERICHOST | NI_NUMERICSERV);
        if (s) {
                LOG_ERROR(cb, "getnameinfo: %s", gai_strerror(s));
                strcpy(host, "(unknown)");
                strcpy(port, "(unknown)");
        }
        memset(&msg, 0, sizeof(msg));
        LOG_INFO(cb, "+++ SER <-- CLI ? CLI_HELLO");
        while ((len = read(ctrl_conn, &msg, sizeof(msg))) == -1) {
                if (errno == EINTR)
                        continue;
                PLOG_ERROR(cb, "read");
                do_close(ctrl_conn);
                goto retry;
        }
        if (memcmp(msg.secret, opts->secret, sizeof(msg.secret)) != 0 ||
            ntohl(msg.type) != CLI_HELLO) {
                if (num_incidents)
                        (*num_incidents)++;
                if (hexdump((void *)&msg, len, dump, sizeof(dump))) {
                        LOG_WARN(cb, "Invalid secret from %s:%s\n%s", host,
                                 port, dump);
                } else
                        LOG_WARN(cb, "Invalid secret from %s:%s", host, port);
                do_close(ctrl_conn);
                goto retry;
        }
        LOG_INFO(cb, "+++ SER <-- CLI   CLI_HELLO -T %d -F %d -l %d -m %" PRIu64,
                 ntohl(msg.num_threads), ntohl(msg.num_flows),
                 ntohl(msg.test_length), be64toh(msg.max_pacing_rate));
        /* tell client that authentication passes */
        msg = (struct hs_msg){ .magic = htonl(opts->magic),
                .type = htonl(SER_ACK),
                .num_threads = htonl(opts->num_threads),
                .num_flows = htonl(opts->num_flows),
                .test_length = htonl(opts->test_length),
        };

        LOG_INFO(cb, "+++ SER --> CLI   SER_ACK -T %d -F %d -l %d",
                        ntohl(msg.num_threads), ntohl(msg.num_flows),
                        ntohl(msg.test_length));
        send_msg(ctrl_conn, &msg, cb, __func__);
        LOG_INFO(cb, "Control connection established with %s:%s", host, port);
        return ctrl_conn;
}


// static void ctrl_wait_client(int ctrl_conn, struct options *opts,
//                              struct callbacks *cb)
static void ctrl_wait_client(tcpconn_t *ctrl_conn_caladan, struct options *opts,
                             struct callbacks *cb)                             
{
        struct hs_msg msg = {.magic = htonl(opts->magic), .type = htonl(CLI_DONE)};

        // if (recv_msg(ctrl_conn, &msg, cb, __func__)) {
        
        if (tcp_read(ctrl_conn_caladan, &msg, sizeof(msg)) <= 0) {                
                LOG_WARN(cb, "Abandoning client");
                return;
        }
        LOG_INFO(cb, "+++ SER <-- CLI   CLI_DONE rate %" PRIu64,
                be64toh(msg.remote_rate));
        opts->remote_rate = be64toh(msg.remote_rate);
}

static void ctrl_wait_client_linux(int ctrl_conn, struct options *opts,
                             struct callbacks *cb)
{
        struct hs_msg msg = {.magic = htonl(opts->magic), .type = htonl(CLI_DONE)};

        if (recv_msg(ctrl_conn, &msg, cb, __func__)) {
                LOG_WARN(cb, "Abandoning client");
                return;
        }
        LOG_INFO(cb, "+++ SER <-- CLI   CLI_DONE rate %" PRIu64,
                be64toh(msg.remote_rate));
        opts->remote_rate = be64toh(msg.remote_rate);
}

static void ctrl_notify_server(tcpconn_t *ctrl_connection, int magic, uint64_t result,
                               struct callbacks *cb)
{
        struct hs_msg msg = { .magic = htonl(magic), .type = htonl(CLI_DONE),
                                .remote_rate = htobe64(result) };
        LOG_INFO(cb, "+++ CLI --> SER   CLI_DONE rate %" PRIu64,
		 be64toh(msg.remote_rate));
        send_msg_caladan(ctrl_connection, &msg, cb, __func__);
        // if (shutdown(ctrl_conn, SHUT_WR))
        //         PLOG_ERROR(cb, "shutdown");
}

struct control_plane {
        struct options *opts;
        struct callbacks *cb;
        int num_incidents;
        int ctrl_conn;
        int ctrl_port;
        struct countdown_cond *data_pending;
        const struct neper_fn *fn;
        int *client_fds;
        tcpconn_t **client_cononections;
        tcpqueue_t *ctrl_queue;
        tcpconn_t *ctrl_connection;
};

struct control_plane* control_plane_create(struct options *opts,
                                           struct callbacks *cb,
                                           struct countdown_cond *data_pending,
                                           const struct neper_fn *fn)
{
        struct control_plane *cp;

        cp = calloc(1, sizeof(*cp));
        cp->opts = opts;
        cp->cb = cb;
        cp->data_pending = data_pending;
        cp->fn = fn;
        return cp;
}

// CALADAN
void control_plane_start(struct control_plane *cp, struct addrinfo **ai, tcpqueue_t *control_plane_q)
{
        if (cp->opts->client) {
                cp->ctrl_connection = ctrl_connect(cp->opts->host,
                                             cp->opts->control_port, ai,
                                             cp->opts, cp->cb);
                LOG_INFO(cp->cb, "connected to control port");
                if (cp->fn->fn_ctrl_client) {
                        cp->fn->fn_ctrl_client(cp->ctrl_conn, cp->cb);
                }
        } else {
                // cp->ctrl_port = ctrl_listen(cp->opts->host,
                //                             cp->opts->control_port, ai,
                //                             cp->opts, cp->cb, &control_plane_q);
                // TODO: Refactor                                            
                // cp->ctrl_queue = control_plane_q;                               

                cp->ctrl_port = ctrl_listen_linux(cp->opts->host,
                                            cp->opts->control_port, ai,
                                            cp->opts, cp->cb);   

                LOG_INFO(cp->cb, "opened control port");
                if (cp-> fn->fn_ctrl_server) {
                        cp->fn->fn_ctrl_server(cp->ctrl_conn, cp->cb);
                }
        }
}

/*
 * Allow users to send SIGALRM or SIGTERM to the client to gracefully stop.
 */
static volatile int termination_requested = 0;
static void sig_alarm_handler(int sig)
{
        termination_requested = 1;
}

void control_plane_wait_until_done(struct control_plane *cp)
{
        if (cp->opts->client) {
                if (cp->opts->test_length > 0) {
                        signal(SIGALRM, sig_alarm_handler);
                        signal(SIGTERM, sig_alarm_handler);
                        alarm(cp->opts->test_length);
                        while (!termination_requested) {
                                sleep(1);
                        }
                        LOG_INFO(cp->cb, "finished sleep");
                } else if (cp->opts->test_length < 0) {
                        countdown_cond_wait(cp->data_pending);
                        LOG_INFO(cp->cb, "finished data wait");
                }
        } else {
                // TODO: Multi-Client support
                const int n = cp->opts->num_clients;
                int* client_fds = calloc(n, sizeof(int));
                int i;
                printf("1!!!!!!!!\n");
                tcpconn_t **client_caladan;
                client_caladan = calloc(n, sizeof(tcpconn_t *));
                printf("1.5!!!!!!!!\n");
                // TODO: Refactor
                if (!client_fds)
                        PLOG_FATAL(cp->cb, "calloc client_fds");

                if (!client_caladan)
                        PLOG_FATAL(cp->cb, "calloc client_caladan");          

                cp->client_fds = client_fds;
                cp->client_cononections = client_caladan;
                printf("2!!!!!!!!\n");
                LOG_INFO(cp->cb, "expecting %d clients", n);
                for (i = 0; i < n; i++) {
                        // client_fds[i] = ctrl_accept(cp->ctrl_port,
                        //                             &cp->num_incidents, cp->cb,
                        //                             cp->opts, cp->ctrl_queue);
                        client_caladan[i] = ctrl_accept(cp->ctrl_port,
                                                    &cp->num_incidents, cp->cb,
                                                    cp->opts, cp->ctrl_queue);
                        LOG_INFO(cp->cb, "client %d connected", i);
                }
                printf("3!!!!!!!!\n");
                // do_close(cp->ctrl_port);  /* disallow further connections */
                tcp_qclose(cp->ctrl_queue);
                printf("4!!!!!!!!\n");
                printf("WAITING FOR CLIENT TO GET DONE\n");
                // while(1);
                //TODO:Post Data transfer control plane
                // if (cp->opts->nonblocking) {
                //         for (i = 0; i < n; i++)
                //                 set_nonblocking(client_fds[i], cp->cb);
                // }
                LOG_INFO(cp->cb, "expecting %d notifications", n);
                for (i = 0; i < n; i++) {
                        ctrl_wait_client(client_caladan[i], (struct options *)cp->opts,
                                         cp->cb);
                        printf("Stop notification from client: %d\n", i);
                        LOG_INFO(cp->cb, "received notification %d", i);
                }
        }
}

//////////////////////////////////////////////////////////
void control_plane_wait_until_done_linux(struct control_plane *cp)
{
        if (cp->opts->client) {
                if (cp->opts->test_length > 0) {
                        signal(SIGALRM, sig_alarm_handler);
                        signal(SIGTERM, sig_alarm_handler);
                        alarm(cp->opts->test_length);
                        while (!termination_requested) {
                                sleep(1);
                        }
                        LOG_INFO(cp->cb, "finished sleep");
                } else if (cp->opts->test_length < 0) {
                        countdown_cond_wait(cp->data_pending);
                        LOG_INFO(cp->cb, "finished data wait");
                }
        } else {
                const int n = cp->opts->num_clients;
                int* client_fds = calloc(n, sizeof(int));
                int i;

                if (!client_fds)
                        PLOG_FATAL(cp->cb, "calloc client_fds");
                cp->client_fds = client_fds;

                LOG_INFO(cp->cb, "expecting %d clients", n);
                for (i = 0; i < n; i++) {
                        printf("Waiting for control_plane connections \n");
                        client_fds[i] = ctrl_accept_linux(cp->ctrl_port,
                                                    &cp->num_incidents, cp->cb,
                                                    cp->opts);
                        LOG_INFO(cp->cb, "client %d connected", i);
                }
                printf("CONTROL PLANE CONNECRION RECEIVED\n");
                do_close(cp->ctrl_port);  /* disallow further connections */
                if (cp->opts->nonblocking) {
                        for (i = 0; i < n; i++)
                                set_nonblocking(client_fds[i], cp->cb);
                }
                LOG_INFO(cp->cb, "expecting %d notifications", n);
                for (i = 0; i < n; i++) {
                        ctrl_wait_client_linux(client_fds[i], (struct options *)cp->opts,
                                         cp->cb);
                        LOG_INFO(cp->cb, "received notification %d", i);
                }
        }
}


void control_plane_stop(struct control_plane *cp)
{
        if (cp->opts->client) {
                struct hs_msg msg = {.magic = htonl(cp->opts->magic), .type = htonl(SER_BYE)};

                ctrl_notify_server(cp->ctrl_connection, cp->opts->magic, cp->opts->local_rate, cp->cb);
                LOG_INFO(cp->cb, "notified server to exit");

                int len = 0;
	        len = tcp_read(cp->ctrl_connection, &msg, sizeof(msg));
                // if (recv_msg(cp->ctrl_conn, &msg, cp->cb, __func__))
                if(len <= 0)
                        LOG_FATAL(cp->cb, "Final handshake mismatch");

                LOG_INFO(cp->cb, "+++ CLI <-- SER   SER_BYE rate %" PRIu64,
			 be64toh(msg.remote_rate));
                ((struct options *)cp->opts)->remote_rate = be64toh(msg.remote_rate);
                // do_close(cp->ctrl_conn);
        } else {
                const int n = cp->opts->num_clients;
                // int *client_fds = cp->client_fds;
                tcpconn_t **client_connections = cp->client_cononections;
                int i;

                for (i = 0; i < n; i++) {
                        struct hs_msg msg = { .magic = htonl(cp->opts->magic),
                                .type = htonl(SER_BYE),
                                .remote_rate = htobe64(cp->opts->local_rate) };
                        LOG_INFO(cp->cb, "+++ SER --> CLI SER_BYE rate %" PRIu64,
                                 be64toh(msg.remote_rate));
                        send_msg_caladan(client_connections[i], &msg, cp->cb, __func__);
                        // do_close(client_fds[i]);
                }
                // free(client_fds);
        }
}

///////////////////////////////////////
void control_plane_stop_linux(struct control_plane *cp)
{
        if (cp->opts->client) {
                struct hs_msg msg = {.magic = htonl(cp->opts->magic), .type = htonl(SER_BYE)};

                ctrl_notify_server(cp->ctrl_conn, cp->opts->magic, cp->opts->local_rate, cp->cb);
                LOG_INFO(cp->cb, "notified server to exit");
                if (recv_msg(cp->ctrl_conn, &msg, cp->cb, __func__))
                        LOG_FATAL(cp->cb, "Final handshake mismatch");
                LOG_INFO(cp->cb, "+++ CLI <-- SER   SER_BYE rate %" PRIu64,
			 be64toh(msg.remote_rate));
                ((struct options *)cp->opts)->remote_rate = be64toh(msg.remote_rate);
                do_close(cp->ctrl_conn);
        } else {
                const int n = cp->opts->num_clients;
                int *client_fds = cp->client_fds;
                int i;

                for (i = 0; i < n; i++) {
                        struct hs_msg msg = { .magic = htonl(cp->opts->magic),
                                .type = htonl(SER_BYE),
                                .remote_rate = htobe64(cp->opts->local_rate) };
                        LOG_INFO(cp->cb, "+++ SER --> CLI SER_BYE rate %" PRIu64,
                                 be64toh(msg.remote_rate));
                        send_msg(client_fds[i], &msg, cp->cb, __func__);
                        do_close(client_fds[i]);
                }
                free(client_fds);
        }
}

int control_plane_incidents(struct control_plane *cp)
{
        return cp->num_incidents;
}

void control_plane_destroy(struct control_plane *cp)
{
        free(cp);
}