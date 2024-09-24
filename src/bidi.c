/*
 * Copyright (C) 2024 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgagroal */
#include <ev.h>
#include <logging.h>
#include <pgagroal.h>
#include <shmem.h>

/* system */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if HAVE_URING
#include <liburing.h>
#include <netdb.h>
#include <sys/eventfd.h>
#endif

#if HAVE_EPOLL
#include <sys/timerfd.h>
#include <sys/epoll.h>
#endif

#if HAVE_KQUEUE
#include <sys/event.h>
#include <sys/types.h>
#include <sys/time.h>
#endif

#define TYPEOF(watcher) watcher->io->type

#define pr_dbg(s) do { printf(s); fflush(stdout); } while (0)
#define SET_ERR(watcher, err) watcher->errcode = err;
#define CLEAN_ERR(watcher) watcher->errcode = 0;

#define for_each(w, first) for (w = first; w; w = w->next)

#define list_add(w, first)    \
        do {                  \
           w->next = first;   \
           first = w;         \
        } while (0)           \

#define list_delete(w, first, target, ret)                                      \
        do {                                                                    \
           for (w = first; *w && *w != target; w = &(*w)->next);                \
           if (!(*w)) {                                                         \
              pgagroal_log_warn("%s: target watcher not found\n", __func__);    \
              ret = EV_ERROR;                                                   \
           } else {                                                             \
              if (!target->next) {                                              \
                 *w = NULL;                                                     \
              } else {                                                          \
                 *w = target->next;                                             \
              }                                                                 \
           }                                                                    \
        } while (0)                                                             \

static int (*loop_init)(struct ev_loop*);
static int (*loop_fork)(struct ev_loop**);
static int (*loop_destroy)(struct ev_loop*);
static int (*loop_start)(struct ev_loop*);
static void (*loop_break)(struct ev_loop*);

static int (*io_start)(struct ev_loop*, struct ev_io*);
static int (*io_stop)(struct ev_loop*, struct ev_io*);
static int io_init(struct ev_io* w, int fd, int event, io_cb cb, void* data, int size, int slot);

static int (*signal_start)(struct ev_loop*, struct ev_signal*);
static int (*signal_stop)(struct ev_loop*, struct ev_signal*);

static int (*periodic_init)(struct ev_periodic*, int);
static int (*periodic_start)(struct ev_loop*, struct ev_periodic*);
static int (*periodic_stop)(struct ev_loop*, struct ev_periodic*);

static bool (*is_running)(struct ev_loop* ev);
static void (*set_running)(struct ev_loop* ev);

static int setup_ops(struct ev_loop*);
static int setup_context(struct ev_context*);

#if HAVE_URING
static int __io_uring_init(struct ev_loop*);
static int __io_uring_destroy(struct ev_loop*);
static int __io_uring_handler(struct ev_loop*, struct io_uring_cqe*);
static int __io_uring_loop(struct ev_loop*);
static int __io_uring_fork(struct ev_loop**);
static int __io_uring_io_start(struct ev_loop*, struct ev_io*);
static int __io_uring_io_stop(struct ev_loop*, struct ev_io*);
static int __io_uring_setup_buffers(struct ev_loop*);
static int __io_uring_periodic_init(struct ev_periodic* w, int msec);
static int __io_uring_periodic_start(struct ev_loop* loop, struct ev_periodic* w);
static int __io_uring_periodic_stop(struct ev_loop* loop, struct ev_periodic* w);
static int __io_uring_signal_handler(struct ev_loop* ev, int signum);
static int __io_uring_signal_start(struct ev_loop* ev, struct ev_signal* w);
static int __io_uring_signal_stop(struct ev_loop* ev, struct ev_signal* w);
static int __io_uring_receive_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe,
                                      bool is_proxy);
static int __io_uring_send_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe);
static int __io_uring_accept_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe);
static int __io_uring_periodic_handler(struct ev_loop* ev, struct ev_periodic* w);
static int __io_uring_bidi_send_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe);
static int __io_uring_bidi_receive_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe);

#endif

#if HAVE_EPOLL
static int __epoll_init(struct ev_loop*);
static int __epoll_destroy(struct ev_loop*);
static int __epoll_handler(struct ev_loop*, void*);
static int __epoll_loop(struct ev_loop*);
static int __epoll_fork(struct ev_loop**);
static int __epoll_io_start(struct ev_loop*, struct ev_io*);
static int __epoll_io_stop(struct ev_loop*, struct ev_io*);
static int __epoll_io_handler(struct ev_loop*, struct ev_io*);
static int __epoll_send_handler(struct ev_loop*, struct ev_io*);
static int __epoll_accept_handler(struct ev_loop*, struct ev_io*);
static int __epoll_receive_handler(struct ev_loop*, struct ev_io*);
static int __epoll_periodic_init(struct ev_periodic*, int);
static int __epoll_periodic_start(struct ev_loop*, struct ev_periodic*);
static int __epoll_periodic_stop(struct ev_loop*, struct ev_periodic*);
static int __epoll_periodic_handler(struct ev_loop*, struct ev_periodic*);
static int __epoll_signal_stop(struct ev_loop*, struct ev_signal*);
static int __epoll_signal_handler(struct ev_loop*);
static int __epoll_signal_start(struct ev_loop*, struct ev_signal*);

#endif

#if HAVE_KQUEUE
static int __kqueue_init(struct ev_loop*);
static int __kqueue_destroy(struct ev_loop*);
static int __kqueue_handler(struct ev_loop*, struct kevent*);
static int __kqueue_loop(struct ev_loop*);
static int __kqueue_fork(struct ev_loop**);
static int __kqueue_io_start(struct ev_loop*, struct ev_io*);
static int __kqueue_io_stop(struct ev_loop*, struct ev_io*);
static int __kqueue_io_handler(struct ev_loop*, struct kevent*);
static int __kqueue_send_handler(struct ev_loop*, struct ev_io*);
static int __kqueue_accept_handler(struct ev_loop*, struct ev_io*);
static int __kqueue_receive_handler(struct ev_loop*, struct ev_io*);
static int __kqueue_periodic_init(struct ev_periodic*, int);
static int __kqueue_periodic_start(struct ev_loop*, struct ev_periodic*);
static int __kqueue_periodic_stop(struct ev_loop*, struct ev_periodic*);
static int __kqueue_periodic_handler(struct ev_loop*, struct kevent*);
static int __kqueue_signal_stop(struct ev_loop*, struct ev_signal*);
static int __kqueue_signal_handler(struct ev_loop*, struct kevent*);
static int __kqueue_signal_start(struct ev_loop*, struct ev_signal*);
#endif

inline static int __attribute__((unused))
set_non_blocking(int fd)
{
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags == -1)
   {
      return EV_ERROR;
   }
   return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

inline static bool __attribute__((unused))
is_non_blocking(int fd)
{
   int flags = fcntl(fd, F_GETFL, 0);
   return (flags & O_NONBLOCK);
}

static inline bool
__is_running(struct ev_loop* ev)
{
   return ev->running;
}
static inline bool
__is_running_atomic(struct ev_loop* ev)
{
   return atomic_load(&ev->atomic_running);
}

static inline void
__set_running(struct ev_loop* ev)
{
   ev->running = true;
}
static inline void
__set_running_atomic(struct ev_loop* ev)
{
   atomic_store(&ev->atomic_running, true);
}

static inline void
__break(struct ev_loop* loop)
{
   loop->running = false;
}
static inline void
__break_atomic(struct ev_loop* loop)
{
   atomic_store(&loop->atomic_running, false);
}

inline static bool
__io_uring_enabled(void)
{
   int fd;
   char res;
   fd = open("/proc/sys/kernel/io_uring_disabled", O_RDONLY);
   if (fd < 0)
   {
      if (errno == ENOENT)
      {
         return true;
      }
      pgagroal_log_fatal("Failed to open file /proc/sys/kernel/io_uring_disabled: %s", strerror(errno));
      exit(1);
   }
   if (read(fd, &res, 1) <= 0)
   {
      pgagroal_log_fatal("Failed to read file /proc/sys/kernel/io_uring_disabled");
      exit(1);
   }
   if (close(fd) < 0)
   {
      pgagroal_log_fatal("Failed to close file descriptor for /proc/sys/kernel/io_uring_disabled: %s", strerror(errno));
      exit(1);
   }

   return res == '0';
}

struct ev_loop*
pgagroal_ev_init(struct configuration* config)
{
   int ret = EV_OK;
   struct ev_loop* ev = calloc(1, sizeof(struct ev_loop));

   if (!config)
   {
      struct configuration default_config = { 0 };
      strcpy(default_config.ev_backend, FALLBACK_BACKEND);
      if (!config)
      {
         config = &default_config;
      }
   }
   ev->config = config;

   ret = setup_context(&ev->ctx);
   if (ret)
   {
      pgagroal_log_error("ev_backend: context setup error\n");
      goto error;
   }

   /* dummy heads */

   ev->ihead.slot = -1;
   ev->ihead.next = NULL;
   ev->shead.slot = -1;
   ev->shead.next = NULL;
   ev->phead.slot = -1;
   ev->phead.next = NULL;

   ret = setup_ops(ev);
   if (ret)
   {
      pgagroal_log_error("setup_ops: setup error\n");
      goto error;
   }

   /* init */

   sigemptyset(&ev->sigset);

   ret = loop_init(ev);
   if (ret)
   {
      pgagroal_log_error("loop_init error");
      goto error;
   }
   return ev;

error:
   free(ev);
   return NULL;
}

int
pgagroal_ev_loop(struct ev_loop* loop)
{
   return loop_start(loop);
}

int
pgagroal_ev_loop_fork(struct ev_loop** loop)
{
   return loop_fork(loop);
}

int
pgagroal_ev_loop_destroy(struct ev_loop* ev)
{
   sigemptyset(&ev->sigset);
   return loop_destroy(ev);
}

void
pgagroal_ev_loop_break(struct ev_loop* ev)
{
   loop_break(ev);
}

int
pgagroal_ev_io_accept_init(struct ev_io* w, int fd, io_cb cb)
{
   return io_init(w, fd, EV_ACCEPT, cb, NULL, 0, -1);
}

int
pgagroal_ev_io_bidi_init(struct ev_io* w, int fd_in, io_cb cb, io_cb cb2, int fd_out)
{
   return io_init(w, fd_in, EV_BIDI, cb, cb2, fd_out, -1);
}

int
pgagroal_ev_io_read_init(struct ev_io* w, int fd, io_cb cb)
{
   return io_init(w, fd, READ, cb, NULL, 0, -1);
}

int
pgagroal_ev_io_send_init(struct ev_io* w, int fd, io_cb cb, void* buf, int buf_len, int bid)
{
   return io_init(w, fd, EV_SEND, cb, buf, buf_len, bid);
}

int
pgagroal_ev_io_receive_init(struct ev_io* w, int fd, io_cb cb)
{
   return io_init(w, fd, EV_RECEIVE, cb, NULL, 0, -1);
}

int
pgagroal_ev_io_connect_init(struct ev_io* w, int fd, io_cb cb, union sockaddr_u* addr)
{
   return io_init(w, fd, CONNECT, cb, (void*)addr, 0, -1);
}

int
pgagroal_ev_io_start(struct ev_loop* ev, struct ev_io* w)
{
   list_add(w, ev->ihead.next);
   return io_start(ev, w);
}

int
pgagroal_ev_io_stop(struct ev_loop* ev, struct ev_io* target)
{
   int ret = EV_OK;
   struct ev_io** w;
   if (!target)
   {
      pgagroal_log_fatal("impossible situation: null pointer provided to stop\n");
   }
   io_stop(ev, target);
   list_delete(w, &ev->ihead.next, target, ret);
   /* pgagroal deals with fd close */
   return ret;
}

int
pgagroal_ev_signal_init(struct ev_signal* w, signal_cb cb, int signum)
{
   w->type = EV_SIGNAL;
   w->signum = signum;
   w->cb = cb;
   w->slot = -1;
   w->next = NULL;
   return EV_OK;
}

int
pgagroal_ev_signal_start(struct ev_loop* ev, struct ev_signal* w)
{
   sigaddset(&ev->sigset, w->signum);
   if (sigprocmask(SIG_BLOCK, &ev->sigset, NULL) == -1)
   {
      pgagroal_log_fatal("sigprocmask");
      exit(1);
   }
   signal_start(ev, w);
   list_add(w, ev->shead.next);
   return EV_OK;
}

int
pgagroal_ev_signal_stop(struct ev_loop* ev, struct ev_signal* target)
{
   int ret = EV_OK;
   struct ev_signal** w;

   if (!target)
   {
      pgagroal_log_error("NULL pointer provided to stop\n");
      return EV_ERROR;
   }

   sigdelset(&ev->sigset, target->signum);

   if (pthread_sigmask(SIG_UNBLOCK, &ev->sigset, NULL) == -1)
   {
      pgagroal_log_error("%s: pthread_sigmask failed\n", __func__);
      return EV_ERROR;
   }

   signal_stop(ev, target);

   list_delete(w, &ev->shead.next, target, ret);

   return ret;
}

int
pgagroal_ev_periodic_init(struct ev_periodic* w, periodic_cb cb, int msec)
{
   if (periodic_init(w, msec))
   {
      pgagroal_log_fatal("%s: __periodic_init failed", __func__);
   }
   w->type = EV_PERIODIC;
   w->slot = -1;
   w->cb = cb;
   w->next = NULL;
   return EV_OK;
}

int
pgagroal_ev_periodic_start(struct ev_loop* loop, struct ev_periodic* w)
{
   periodic_start(loop, w);
   list_add(w, loop->phead.next);
   return EV_OK;
}

int
pgagroal_ev_periodic_stop(struct ev_loop* ev, struct ev_periodic* target)
{
   int ret = EV_OK;
   struct ev_periodic** w;
   if (!target)
   {
      pgagroal_log_error("null pointer provided to stop\n");
      return EV_ERROR;
   }
   ret = periodic_stop(ev, target);
   list_delete(w, &ev->phead.next, target, ret);
   return ret;
}

static int
setup_ops(struct ev_loop* ev)
{
   int ret = EV_OK;
   bool mtt = ev->ctx.multithreading;
   struct configuration* config = (struct configuration*)shmem;

   is_running = mtt ? __is_running_atomic : __is_running;
   set_running = mtt ? __set_running_atomic: __set_running;
   loop_break = mtt ? __break_atomic: __break;

   if (!strcmp(config->ev_backend, "io_uring"))
   {
#if HAVE_URING
      loop_init = __io_uring_init;
      loop_fork = __io_uring_fork;
      loop_destroy = __io_uring_destroy;
      loop_start = __io_uring_loop;
      io_start = __io_uring_io_start;
      io_stop = __io_uring_io_stop;
      periodic_init = __io_uring_periodic_init;
      periodic_start = __io_uring_periodic_start;
      periodic_stop = __io_uring_periodic_stop;
      signal_start = __io_uring_signal_start;
      signal_stop = __io_uring_signal_stop;
#endif
   }
   if (!strcmp(config->ev_backend, "epoll"))
   {
#if HAVE_EPOLL
      loop_init = __epoll_init;
      loop_fork = __epoll_fork;
      loop_destroy = __epoll_destroy;
      loop_start = __epoll_loop;
      io_start = __epoll_io_start;
      io_stop = __epoll_io_stop;
      periodic_init = __epoll_periodic_init;
      periodic_start = __epoll_periodic_start;
      periodic_stop = __epoll_periodic_stop;
      signal_start = __epoll_signal_start;
      signal_stop = __epoll_signal_stop;
#endif
   }
   else if (!strcmp(config->ev_backend, "kqueue"))
   {
#if HAVE_KQUEUE
      loop_init = __kqueue_init;
      loop_fork = __kqueue_fork;
      loop_destroy = __kqueue_destroy;
      loop_start = __kqueue_loop;
      io_start = __kqueue_io_start;
      io_stop = __kqueue_io_stop;
      periodic_init = __kqueue_periodic_init;
      periodic_start = __kqueue_periodic_start;
      periodic_stop = __kqueue_periodic_stop;
      signal_start = __kqueue_signal_start;
      signal_stop = __kqueue_signal_stop;
#endif
   }

   return ret;
}

/*
 * TODO: move this to libpgagroal/configuration.c and allow configuration
 */
static int
setup_context(struct ev_context* ctx)
{
   struct configuration* config = &((struct main_configuration*)shmem)->common;
   /* ordered from highest to lowest priority */
   char* backends[] = {
#if HAVE_URING
      "io_uring",
#endif
#if HAVE_EPOLL
      "epoll",
#endif
#if HAVE_KQUEUE
      "kqueue",
#endif
   };
   char log[] = (
#if HAVE_URING
      "io_uring, "
#endif
#if HAVE_EPOLL
      "epoll, "
#endif
#if HAVE_KQUEUE
      "kqueue, "
#endif
      );

   if (sizeof(backends) == 0)
   {
      pgagroal_log_fatal("no ev_backend supported");
      exit(1);
   }

   log[strlen(log) - 2] = '\0';
   pgagroal_log_debug("Available ev backends: %s", log);

   if (!strnlen(config->ev_backend, MISC_LENGTH))
   {
      pgagroal_log_warn("ev_backend not set in configuration file");
      pgagroal_log_warn("ev_backend automatically set to: 'auto'");
      strcpy(config->ev_backend, "auto");
   }

   /* if auto, select the first supported backend */
   if (!strcmp(config->ev_backend, "auto"))
   {
      strcpy(config->ev_backend, backends[0]);
   }

   pgagroal_log_debug("Selected backend: '%s'", config->ev_backend);

   if (!strcmp(config->ev_backend, "io_uring"))
   {
      if (!__io_uring_enabled())
      {
         pgagroal_log_warn("io_uring supported but not enabled. Enable io_uring by setting /proc/sys/kernel/io_uring_disabled to '0'");
         pgagroal_log_warn("Fallback configured to 'epoll'");
         strcpy(config->ev_backend, "epoll");
      }
      else if (config->tls)
      {
         pgagroal_log_warn("ev_backend '%s' not supported with tls on");
         pgagroal_log_warn("Fallback configured to 'epoll'");
         strcpy(config->ev_backend, "epoll");
      }

#if HAVE_URING
      if (ctx->defer_tw && ctx->sqpoll)
      {
         pgagroal_log_fatal("cannot use DEFER_TW and SQPOLL at the same time\n");
         exit(1);
      }

      ctx->entries = 32;
      ctx->params.cq_entries = 64;
      ctx->params.flags = 0;
      ctx->params.flags |= IORING_SETUP_SINGLE_ISSUER;
      ctx->params.flags |= IORING_SETUP_CLAMP;
      ctx->params.flags |= IORING_SETUP_CQSIZE;
      /* ctx->params.flags |= IORING_FEAT_NODROP */

      /* default configuration */

      if (ctx->sqpoll)
      {
         ctx->params.flags |= IORING_SETUP_SQPOLL;
         ctx->params.flags ^= IORING_SETUP_DEFER_TASKRUN;
      }
      if (!ctx->sqpoll && !ctx->defer_tw)
      {
         ctx->params.flags |= IORING_SETUP_COOP_TASKRUN;
      }
      if (!ctx->buf_count)
      {
         ctx->buf_count = BUFFER_COUNT;
      }
      if (!ctx->buf_size)
      {
         ctx->buf_size = BUFFER_SIZE;
      }
      ctx->br_mask = (ctx->buf_count - 1);

      if (ctx->fixed_files)
      {
         pgagroal_log_fatal("no support for fixed files\n");      /* TODO: add support for fixed files */
         exit(1);
      }
#endif
   }
   else if (!strcmp(config->ev_backend, "epoll"))
   {
#if HAVE_EPOLL
      ctx->epoll_flags = 0;
#endif
   }

   ctx->multithreading = false;

   return EV_OK;
}

static int
io_init(struct ev_io* w, int fd, int event, io_cb cb, void* data, int size, int slot)
{
   w->fd = fd;
   w->type = event;
   w->cb = cb;
   w->data = data;
   w->size = size;
   w->slot = slot;
   w->bid = -1;
   w->errcode = 0;

   if (w->type == EV_BIDI)
   {
      w->fd_out = size;
      w->cb2 = (io_cb) data;
   }
   return EV_OK;
}

#if HAVE_URING
static inline struct io_uring_sqe*
__io_uring_get_sqe(struct ev_loop* ev)
{
   struct io_uring* ring = &ev->ring;
   struct io_uring_sqe* sqe;
   do /* necessary if SQPOLL, but I don't think there is an advantage of using SQPOLL */
   {
      sqe = io_uring_get_sqe(ring);
      if (sqe)
      {
         return sqe;
      }
      else
      {
         io_uring_sqring_wait(ring);
      }
   }
   while (1);
}

static inline int
__io_uring_rearm_receive(struct ev_loop* ev, struct ev_io* w)
{
   struct io_uring_sqe* sqe = __io_uring_get_sqe(ev);
   io_uring_prep_recv_multishot(sqe, w->fd, NULL, 0, 0);
   io_uring_sqe_set_data(sqe, w);
   sqe->flags |= IOSQE_BUFFER_SELECT;
   sqe->buf_group = IN_BR_BGID;
   return EV_OK;
}

static inline int
__io_uring_replenish_buffers(struct ev_loop* ev, struct io_buf_ring* br, int bid_start, int bid_end)
{
   int count;
   struct ev_context ctx = ev->ctx;
   if (bid_end >= bid_start)
   {
      count = (bid_end - bid_start);
   }
   else
   {
      count = (bid_end + ctx.buf_count - bid_start);
   }
   for (int i = bid_start; i != bid_end; i = (i + 1) & (ctx.buf_count - 1))
   {
      io_uring_buf_ring_add(br->br, (void*)br->br->bufs[i].addr, ctx.buf_size, i, ctx.br_mask, 0);
   }
   io_uring_buf_ring_advance(br->br, count);
   return EV_OK;
}

static int
__io_uring_init(struct ev_loop* loop)
{
   int ret = EV_OK;
   ret = io_uring_queue_init_params(loop->ctx.entries, &loop->ring, &loop->ctx.params);  /* on fork: gets a new ring */
   if (ret)
   {
      pgagroal_log_fatal("io_uring_queue_init_params: %s\n", strerror(-ret));
   }
   if (!loop->ctx.no_use_buffers)
   {
      ret = __io_uring_setup_buffers(loop);
      if (ret)
      {
         pgagroal_log_fatal("%s: __io_uring_setup_buffers: %s\n", __func__, strerror(-ret));
      }
   }
   return ret;
}

static int
__io_uring_destroy(struct ev_loop* ev)
{
   /* free buffer rings */
   io_uring_free_buf_ring(&ev->ring, ev->in_br.br, ev->ctx.buf_count, ev->in_br.bgid);
   ev->in_br.br = NULL;
   io_uring_free_buf_ring(&ev->ring, ev->out_br.br, ev->ctx.buf_count, ev->out_br.bgid);
   ev->out_br.br = NULL;
   if (ev->ctx.use_huge)
   {
      /* TODO: munmap(cbr->buf, buf_size * nr_bufs); */
   }
   else
   {
      free(ev->in_br.buf);
      free(ev->out_br.buf);
   }
   io_uring_queue_exit(&ev->ring);
   free(ev);
   return EV_OK;
}

static int
__io_uring_io_start(struct ev_loop* ev, struct ev_io* w)
{
   int domain;
   union sockaddr_u* addr;
   struct io_uring_sqe* sqe = __io_uring_get_sqe(ev);
   io_uring_sqe_set_data(sqe, w);
   switch (w->type)
   {
      case EV_ACCEPT:
         io_uring_prep_multishot_accept(sqe, w->fd, NULL, NULL, 0);
         break;
      case EV_BIDI:
      case EV_RECEIVE:
         //printf("%s: ev_receive\n", __func__); fflush(stdout);
         io_uring_prep_recv(sqe, w->fd, NULL, 0, 0);
         sqe->flags |= IOSQE_BUFFER_SELECT | MSG_WAITALL;
         sqe->buf_group = IN_BR_BGID;
         break;
      case EV_SEND:
         //printf("%s: ev_send\n", __func__); fflush(stdout);
         io_uring_prep_send(sqe, w->fd, w->data, w->size, 0); /* TODO: flags */
         sqe->buf_group = OUT_BR_BGID;
         break;
      case CONNECT:
         addr = (union sockaddr_u*)w->data;
         if (ev->ctx.ipv6)
         {
            io_uring_prep_connect(sqe, w->fd, (struct sockaddr*) &addr->addr6, sizeof(struct sockaddr_in6));
         }
         else
         {
            io_uring_prep_connect(sqe, w->fd, (struct sockaddr*) &addr->addr4, sizeof(struct sockaddr_in));
         }
         break;
      case SOCKET:
         if (ev->ctx.ipv6)
         {
            domain = AF_INET6;
         }
         else
         {
            domain = AF_INET;
         }
         io_uring_prep_socket(sqe, domain, SOCK_STREAM, 0, 0);
         break;
      case READ: /* unused */
         io_uring_prep_read(sqe, w->fd, w->data, w->size, 0);
         break;
      default:
         pgagroal_log_fatal("%s: unknown event type: %d\n", __func__, w->type);
         return EV_ERROR;
   }
   return EV_OK;
}

static int
__io_uring_io_stop(struct ev_loop* ev, struct ev_io* target)
{
   int ret = EV_OK;
   struct io_uring_sqe* sqe;
   sqe = io_uring_get_sqe(&ev->ring);
   io_uring_prep_cancel64(sqe, (uint64_t)target, 0); /* TODO: flags? */
   return ret;
}

static int
__io_uring_signal_start(struct ev_loop* ev, struct ev_signal* w)
{
   return EV_OK;
}

static int
__io_uring_signal_stop(struct ev_loop* ev, struct ev_signal* w)
{
   return EV_OK;
}

static int
__io_uring_periodic_init(struct ev_periodic* w, int msec)
{
   /* TODO: how optimized is designated initializers really */
   w->ts = (struct __kernel_timespec) {
      .tv_sec = msec / 1000,
      .tv_nsec = (msec % 1000) * 1000000
   };
   return EV_OK;
}

static int
__io_uring_periodic_start(struct ev_loop* loop, struct ev_periodic* w)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring);
   io_uring_sqe_set_data(sqe, w);
   io_uring_prep_timeout(sqe, &w->ts, 0, IORING_TIMEOUT_MULTISHOT);
   return EV_OK;
}

static int
__io_uring_periodic_stop(struct ev_loop* loop, struct ev_periodic* w)
{
   struct io_uring_sqe* sqe;
   sqe = io_uring_get_sqe(&loop->ring);
   io_uring_prep_cancel64(sqe, (uint64_t)w, 0); /* TODO: flags? */
   return EV_OK;
}

/*
 * Based on: https://git.kernel.dk/cgit/liburing/tree/examples/proxy.c
 * (C) 2024 Jens Axboe <axboe@kernel.dk>
 */
static int
__io_uring_loop(struct ev_loop* ev)
{
   int ret;
   int signum;
   int events;
   int to_wait = 1; /* wait for any 1 */
   unsigned int head;
   struct io_uring_cqe* cqe;
   struct __kernel_timespec* ts;
   struct __kernel_timespec idle_ts = {
      .tv_sec = 0,
      .tv_nsec = 100000000LL
   };
   struct timespec timeout = {
      .tv_sec = 0,
      .tv_nsec = 0
   };

   set_running(ev);
   while (is_running(ev))
   {
      ts = &idle_ts;
      io_uring_submit_and_wait_timeout(&ev->ring, &cqe, to_wait, ts, NULL);

      /* Good idea to leave here to see what happens */
      if (*ev->ring.cq.koverflow)
      {
         pgagroal_log_error("io_uring overflow %u\n", *ev->ring.cq.koverflow);
         exit(EXIT_FAILURE);
      }
      if (*ev->ring.sq.kflags & IORING_SQ_CQ_OVERFLOW)
      {
         pgagroal_log_error("io_uring overflow\n");
         exit(EXIT_FAILURE);
      }

      /* Check for signals before iterating over cqes */
      signum = sigtimedwait(&ev->sigset, NULL, &timeout);
      if (signum > 0)
      {
         ret = __io_uring_signal_handler(ev, signum);

         if (ret == EV_ERROR)
         {
            pgagroal_log_error("Signal handling error\n");
            return EV_ERROR;
         }
         if (!is_running(ev))
         {
            break;
         }
      }

      events = 0;
      io_uring_for_each_cqe(&(ev->ring), head, cqe)
      {
         /* Currently closing the main connection fd means that pgagroal loop
          * will stop, so just return an error to the caller. If the caller
          * eventually decides to continue the loop, the
          * caller will have to handle this error.
          */
         ret = __io_uring_handler(ev, cqe);
         if (ret == EV_CLOSE_FD)
         {
            return EV_CLOSE_FD;
         }
         if (ret == EV_ERROR)
         {
            pgagroal_log_error("__io_uring_handler error\n");
            return EV_ERROR;
         }
         events++;
      }
      if (events)
      {
         io_uring_cq_advance(&ev->ring, events);  /* batch marking as seen */
      }

      /* TODO: housekeeping ? */

   }
   return EV_OK;
}

static int
__io_uring_fork(struct ev_loop** loop)
{
   struct ev_loop* tmp = *loop;
   *loop = pgagroal_ev_init(tmp->config);
   __io_uring_destroy(tmp);

   return EV_OK;
}

static int
__io_uring_handler(struct ev_loop* ev, struct io_uring_cqe* cqe)
{
   int ret = EV_OK;
   ev_watcher w;
   w.io = (ev_io*)io_uring_cqe_get_data(cqe);

   /*
    * Cancelled requests will trigger the handler, but have NULL data.
    */
   if (!w.io)
   {
      return EV_OK;
   }

   /* io handler */
   //printf("%s: entering %d\n", __func__, w.io->type); fflush(stdout);
   switch (w.io->type)
   {
      case EV_PERIODIC:
         return __io_uring_periodic_handler(ev, w.periodic);
      case EV_ACCEPT:
         return __io_uring_accept_handler(ev, w.io, cqe);
      case EV_BIDI:
         //printf("%s: got to ev_bidi bid=%d\n", __func__, w.io->bid); fflush(stdout);
         if (w.io->bid < 0)
         {
            return __io_uring_bidi_receive_handler(ev, w.io, cqe);
         }
         else
         {
            return __io_uring_bidi_send_handler(ev, w.io, cqe);
         }
         break;
      case EV_SEND:
         return __io_uring_send_handler(ev, w.io, cqe);

      case EV_RECEIVE:
retry:
         ret = __io_uring_receive_handler(ev, w.io, cqe, false);
         switch (ret)
         {
            case EV_CLOSE_FD: /* connection closed */
               /* pgagroal deals with closing fd */
               break;
            case EV_REPLENISH_BUFFERS: /* TODO: stress test. Buffers should be replenished after each recv. */
               pgagroal_log_warn("__io_uring_receive_handler: request requeued\n");
               exit(1);
               usleep(100);
               goto retry;
               break;
         }
         break;
      default:
         pgagroal_log_fatal("%s: _io_handler: event not found eventno=%d", __func__, w.io->type);
   }
   return ret;
}

static int
__io_uring_periodic_handler(struct ev_loop* ev, struct ev_periodic* w)
{
   w->cb(ev, w, 0);
   return EV_OK;
}

static int
__io_uring_bidi_send_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe)
{
   // int revents = EV_OK;
   struct io_buf_ring* in_br = &ev->in_br;
   // // struct io_buf_ring* out_br = &ev->out_br;
   struct ev_context ctx = ev->ctx;
   //printf("%s: entering\n", __func__); fflush(stdout);
   if (!cqe->res)
   {
      w->errcode = EV_ERROR;
      w->cb2(ev, w, EV_ERROR);
   }
   // __io_uring_rearm_receive(ev, w);

#if 1 /* TODO : #if DEBUG */
   assert (w->bid >= 0);
#endif

   io_uring_buf_ring_add(in_br->br, (void*) in_br->br->bufs[w->bid].addr, ctx.buf_size, w->bid, ctx.br_mask, 0);
   io_uring_buf_ring_advance(in_br->br, 1);

   w->bid = -1;

   assert (w->bid < 0);
   struct io_uring_sqe* sqe = __io_uring_get_sqe(ev);
   io_uring_sqe_set_data(sqe, w);
   io_uring_prep_recv(sqe, w->fd, NULL, 0, 0);
   sqe->flags |= IOSQE_BUFFER_SELECT | MSG_WAITALL;
   sqe->buf_group = IN_BR_BGID;
   return EV_OK;
}

static int
__io_uring_bidi_receive_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe)
{

   int ret = EV_OK;
   int bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
   struct ev_context ctx = ev->ctx;
   struct io_buf_ring* in_br = &ev->in_br;
   //struct io_buf_ring* out_br = &ev->out_br;
   int total_in_bytes;
   //printf("%s: entering\n", __func__); fflush(stdout);

   if (cqe->res == -ENOBUFS)
   {
      pgagroal_log_warn("io_receive_handler: Not enough buffers\n");
      return EV_REPLENISH_BUFFERS;
   }

   if (!(cqe->flags & IORING_CQE_F_BUFFER))
   {
      if (!(cqe->res)) /* Closed connection */
      {
         return EV_CLOSE_FD;
      }
   }

   /* From the docs: https://man7.org/linux/man-pages/man3/io_uring_prep_recv_multishot.3.html
    * "If a posted CQE does not have the IORING_CQE_F_MORE flag set then the multishot receive will be
    * done and the application should issue a new request."
    */
   // if (!(cqe->flags & IORING_CQE_F_MORE))
   // {
   //    pgagroal_log_warn("need to rearm receive: added timeout");
   //    ret = __io_uring_rearm_receive(ev, w);
   //    if (ret)
   //    {
   //       return EV_ERROR;
   //    }
   // }

   total_in_bytes = cqe->res;
   if (total_in_bytes >= MAX_BUFFER_SIZE)
   {
      pgagroal_log_fatal("unexpected");
      exit(1);
   }

   w->data = in_br->buf + (bid * ctx.buf_size);
   w->size = total_in_bytes;
   w->bid = bid;
   w->cb(ev, w, ret);

   struct io_uring_sqe* sqe = __io_uring_get_sqe(ev);
   io_uring_sqe_set_data(sqe, w);
   // printf("%s: w->data=%s\n", __func__, (char*)w->data);
   io_uring_prep_send(sqe, w->fd_out, w->data, w->size, 0);
   sqe->buf_group = OUT_BR_BGID;
   // io_uring_submit(&ev->ring);
   //printf("%s: exiting\n", __func__); fflush(stdout);

   // io_uring_buf_ring_add(in_br->br, (void*)in_br->br->bufs[bid].addr, ctx.buf_size, bid, ctx.br_mask, 0);
   // io_uring_buf_ring_advance(in_br->br, 1);

   return EV_OK;
}

static int
__io_uring_accept_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe)
{
   w->fd_out = cqe->res;
   w->cb(ev, w, 0);
   return EV_OK;
}

static int
__io_uring_send_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe)
{
   int revents = EV_OK;
   if (!cqe->res)
   {
      revents = EV_ERROR;
   }
   w->cb(ev, w, revents);

   return EV_OK;
}

static int
__io_uring_signal_handler(struct ev_loop* ev, int signum)
{
   struct ev_signal* w;
   for (w = ev->shead.next; w && w->signum != signum; w = w->next)
   {
      /* empty */;
   }
   if (!w)
   {
      pgagroal_log_error("no watcher for signal %d\n", signum);
      exit(EXIT_FAILURE);
   }
   w->cb(ev, w, 0);
   return EV_OK;
}

static int
__io_uring_receive_handler(struct ev_loop* ev, struct ev_io* w, struct io_uring_cqe* cqe, bool is_proxy)
{
   int ret = EV_OK;
   int bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
   struct ev_context ctx = ev->ctx;
   struct io_buf_ring* in_br = &ev->in_br;
   int total_in_bytes;

   if (cqe->res == -ENOBUFS)
   {
      pgagroal_log_warn("io_receive_handler: Not enough buffers\n");
      return EV_REPLENISH_BUFFERS;
   }

   if (!(cqe->flags & IORING_CQE_F_BUFFER))
   {
      if (!(cqe->res)) /* Closed connection */
      {
         return EV_CLOSE_FD;
      }
   }

   /* From the docs: https://man7.org/linux/man-pages/man3/io_uring_prep_recv_multishot.3.html
    * "If a posted CQE does not have the IORING_CQE_F_MORE flag set then the multishot receive will be
    * done and the application should issue a new request."
    */
   if (!(cqe->flags & IORING_CQE_F_MORE))
   {
      pgagroal_log_warn("need to rearm receive: added timeout");
      ret = __io_uring_rearm_receive(ev, w);
      if (ret)
      {
         return EV_ERROR;
      }
   }

   total_in_bytes = cqe->res;
   if (total_in_bytes >= MAX_BUFFER_SIZE)
   {
      pgagroal_log_fatal("unexpected");
      exit(1);
   }

   /* This is not valid anymore as the buffers can fit anything.
    * If the size of the buffer (this_bytes) is greater than the size of the received bytes, then continue.
    * Otherwise, we iterate over another buffer.
    */
   // in_bytes = cqe->res;
   // while (in_bytes)
   // {
   // buf = &(in_br->br->bufs[bid]);
   // data = (char*) buf->addr;
   // this_bytes = buf->len;

   /* Break if the received bytes is smaller than buffer length.
    * Otherwise, continue iterating over the buffers. */
   // if (this_bytes > in_bytes)
   // {
   //    this_bytes = in_bytes;
   // }

   // io_uring_buf_ring_add(out_br->br, data, this_bytes, bid, ctx.br_mask, 0);
   // io_uring_buf_ring_advance(out_br->br, 1);

   // in_bytes -= this_bytes;

   // *bid = (*bid + 1) & (ctx.buf_count - 1);
   // }

   w->data = in_br->buf + (bid * ctx.buf_size);
   w->size = total_in_bytes;
   w->bid = bid;
   w->cb(ev, w, ret);

   /* return buffer to the pool */
   /* get first available out_br */
   // if (w->type == EV_BIDI)
   // {
   //         io_uring_buf_ring_add(out_br->br, (void*)in_br->br->bufs[bid].addr, ctx.buf_size, bid, ctx.br_mask, 0);
   //         io_uring_buf_ring_advance(out_br->br, 1);
   // }
   // io_uring_buf_ring_add(in_br->br, (void*)in_br->br->bufs[bid].addr, ctx.buf_size, bid, ctx.br_mask, 0);
   // io_uring_buf_ring_advance(in_br->br, 1);

   return EV_OK;
}

static int
__io_uring_setup_buffers(struct ev_loop* ev)
{
   int ret = EV_OK;
   void* ptr;
   struct ev_context ctx = ev->ctx;

   struct io_buf_ring* in_br = &ev->in_br;
   struct io_buf_ring* out_br = &ev->out_br;

   if (ctx.use_huge)
   {
      pgagroal_log_warn("use_huge not implemented yet\n"); /* TODO */
   }
   if (posix_memalign(&in_br->buf, ALIGNMENT, ctx.buf_count * ctx.buf_size))
   {
      pgagroal_log_fatal("posix_memalign");
      exit(1);
   }

   in_br->br = io_uring_setup_buf_ring(&ev->ring, ctx.buf_count, IN_BR_BGID, 0, &ret);
   out_br->br = io_uring_setup_buf_ring(&ev->ring, ctx.buf_count, OUT_BR_BGID, 0, &ret);
   if (!in_br->br || !out_br->br)
   {
      pgagroal_log_fatal("buffer ring register failed %d\n", ret);
      exit(1);
   }

   ptr = in_br->buf;
   for (int i = 0; i < ctx.buf_count; i++)
   {
      io_uring_buf_ring_add(in_br->br, ptr, ctx.buf_size, i, ctx.br_mask, i);
      ptr += ctx.buf_size;
   }
   io_uring_buf_ring_advance(in_br->br, ctx.buf_count);

   // ptr = in_br->buf;
   // out_br->available = 0;
   // for (int i = 0; i < ctx.buf_count; i++)
   // {
   //    io_uring_buf_ring_add(out_br->br, ptr, ctx.buf_size, i, ctx.br_mask, i);
   //    ptr += ctx.buf_size;
   // }
   // io_uring_buf_ring_advance(out_br->br, ctx.buf_count);

   return ret;
}

void
_next_bid(struct ev_loop* ev, int* bid)
{
   struct ev_context ctx = ev->ctx;
   *bid = (*bid + 1) % ctx.buf_count;
}
#endif

/********************************************************************************
 *                                                                               *
 *                                     EPOLL                                     *
 *                                                                               *
 *********************************************************************************/

#if HAVE_EPOLL

int
__epoll_loop(struct ev_loop* loop)
{
   int ret;
   int nfds;
   struct epoll_event events[MAX_EVENTS];
   int timeout = 10;
   struct epoll_event ev = {
      .events = EPOLLIN, /* | EPOLLET */
      .data.fd = signalfd(-1, &loop->sigset, 0),
   };
   if (ev.data.fd == -1)
   {
      pgagroal_log_fatal("signalfd");
      exit(1);
   }
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, ev.data.fd, &ev) == -1)
   {
      pgagroal_log_fatal("epoll_ctl (signalfd)");
      exit(1);
   }

   set_running(loop);
   while (is_running(loop))
   {
      /* TODO: see if using epoll_pwait2 is better than current implementation
       *
       * nfds = epoll_pwait2(loop->epollfd, events, MAX_EVENTS, &timeout, NULL);
       */
      nfds = epoll_wait(loop->epollfd, events, MAX_EVENTS, timeout);

      if (!is_running(loop))
      {
         break;
      }
      for (int i = 0; i < nfds; i++)
      {
         if (events[i].data.fd == ev.data.fd)
         {
            ret = __epoll_signal_handler(loop);
         }
         else
         {
            ret = __epoll_handler(loop, (void*)events[i].data.u64);
            /* Currently closing the main connection fd means that pgagroal loop
             * will stop, so just return an error to the caller. If the caller
             * eventually decides to continue the loop, the
             * caller will have to handle this error.
             */
            if (ret == EV_CLOSE_FD)
            {
               return EV_CLOSE_FD;
            }
            if (ret == EV_ERROR)
            {
               pgagroal_log_error("handler error");
               return EV_ERROR;
            }
         }
      }
   }
   return EV_OK;
}

static int
__epoll_init(struct ev_loop* ev)
{
   ev->buffer = malloc(sizeof(char) * (MAX_BUFFER_SIZE));
   ev->epollfd = epoll_create1(ev->ctx.epoll_flags);
   if (ev->epollfd == -1)
   {
      pgagroal_log_error("epoll init error");
      return EV_ERROR;
   }

   return EV_OK;
}

static int
__epoll_fork(struct ev_loop** parent_loop)
{
   /* TODO destroy everything related to loop */
   if (sigprocmask(SIG_UNBLOCK, &(*parent_loop)->sigset, NULL) == -1)
   {
      pgagroal_log_fatal("sigprocmask");
      exit(1);
   }
   sigemptyset(&(*parent_loop)->sigset);
   close((*parent_loop)->epollfd);
   return EV_OK;
}

static int
__epoll_destroy(struct ev_loop* ev)
{
   close(ev->epollfd);
   free(ev);
   return EV_OK;
}

static int
__epoll_handler(struct ev_loop* ev, void* wp)
{
   struct ev_periodic* w = (struct ev_periodic*)wp;
   if (w->type == EV_PERIODIC)
   {
      return __epoll_periodic_handler(ev, (struct ev_periodic*)w);
   }
   return __epoll_io_handler(ev, (struct ev_io*)w);
}

static int
__epoll_signal_start(struct ev_loop* ev, struct ev_signal* w)
{

   return EV_OK;
}

static int
__epoll_signal_stop(struct ev_loop* ev, struct ev_signal* w)
{
   return EV_OK;
}

static int
__epoll_signal_handler(struct ev_loop* ev)
{
   struct ev_signal* w;
   siginfo_t siginfo;
   int signo;
   signo = sigwaitinfo(&ev->sigset, &siginfo);
   if (signo == -1)
   {
      pgagroal_log_error("sigwaitinfo");
      return EV_ERROR;
   }

   for_each(w, ev->shead.next)
   {
      if (w->signum == signo)
      {
         w->cb(ev, w, 0);
         return EV_OK;
      }
   }

   pgagroal_log_error("No handler found for signal %d\n", signo);
   return EV_ERROR;
}

static int
__epoll_periodic_init(struct ev_periodic* w, int msec)
{
   struct timespec now;
   struct itimerspec new_value;

   if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) /* TODO: evaluate what kind of clock to use (!) */
   {
      pgagroal_log_error("clock_gettime");
      return EV_ERROR;
   }

   new_value.it_value.tv_sec = msec / 1000;
   new_value.it_value.tv_nsec = (msec % 1000) * 1000000;

   new_value.it_interval.tv_sec = msec / 1000;
   new_value.it_interval.tv_nsec = (msec % 1000) * 1000000;

   w->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);  /* no need to set it to non-blocking due to TFD_NONBLOCK */
   if (w->fd == -1)
   {
      perror("timerfd_create");
      return EV_ERROR;
   }

   if (timerfd_settime(w->fd, 0, &new_value, NULL) == -1)
   {
      perror("timerfd_settime");
      close(w->fd);
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__epoll_periodic_start(struct ev_loop* loop, struct ev_periodic* w)
{
   struct epoll_event event;
   event.events = EPOLLIN; /* | EPOLLET */
   event.data.u64 = (uint64_t)w;
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, w->fd, &event) == -1)
   {
      perror("epoll_ctl");
      close(w->fd);
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__epoll_periodic_stop(struct ev_loop* loop, struct ev_periodic* w)
{
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, w->fd, NULL) == -1)
   {
      pgagroal_log_error("%s: epoll_ctl: delete failed", __func__);
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__epoll_periodic_handler(struct ev_loop* ev, struct ev_periodic* w)
{
   uint64_t exp;
   int nread = read(w->fd, &exp, sizeof(uint64_t));
   if (nread != sizeof(uint64_t))
   {
      pgagroal_log_error("periodic_handler: read");
      return EV_ERROR;
   }
   w->cb(ev, w, 0);
   return EV_OK;
}

static int
__epoll_io_start(struct ev_loop* ev, struct ev_io* w)
{
   struct epoll_event event;
   switch (w->type)
   {
      case EV_ACCEPT:
      case EV_RECEIVE:
         event.events = EPOLLIN; /*  | EPOLLET */
         break;
      case EV_SEND:
         event.events = EPOLLOUT; /*  | EPOLLET */
         break;
      default:
         pgagroal_log_fatal("%s: unknown event type: %d\n", __func__, w->type);
         return EV_ERROR;
   }
   if (set_non_blocking(w->fd)) /* TODO: err handling */
   {
      pgagroal_log_fatal("%s: set_non_blocking");
      exit(1);
   }
   event.data.u64 = (uint64_t)w;

   if (epoll_ctl(ev->epollfd, EPOLL_CTL_ADD, w->fd, &event) == -1)
   {
      pgagroal_log_fatal("%s: epoll_ctl");
      exit(1);
      close(w->fd);
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__epoll_io_stop(struct ev_loop* ev, struct ev_io* target)
{
   int ret = EV_OK;
   bool fd_is_open = fcntl(target->fd, F_GETFD) != -1 || errno != EBADF;

   /* TODO: pgagroal deals with closing fds, so dealing with EPOLL_CTL_DEL may be unnecessary */
   if (fd_is_open)
   {
      if (epoll_ctl(ev->epollfd, EPOLL_CTL_DEL, target->fd, NULL) == -1)
      {
         ret = EV_ERROR;
      }
   }

   return ret;
}

static int
__epoll_io_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   switch (w->type)
   {
      case EV_ACCEPT:
         return __epoll_accept_handler(ev, w);
      case EV_SEND:
         return __epoll_send_handler(ev, w);
      case EV_RECEIVE:
         ret = __epoll_receive_handler(ev, w);
         switch (ret)
         {
            case EV_CLOSE_FD: /* connection closed */
               /* pgagroal deals with closing fd, so either remove this here or there */
               break;
         }
         break;
      default:
         pgagroal_log_fatal("%s: unknown value for event type %d\n", __func__);
   }

   return ret;
}

static int
__epoll_receive_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   int nrecv = 0;
   int total_recv = 0;
   void* buf = ev->buffer;
   if (!buf)
   {
      perror("malloc error");
      return EV_ALLOC_ERROR;
   }

   if (!w->ssl)
   {
      while (1)
      {
         nrecv = recv(w->fd, buf + total_recv, MAX_BUFFER_SIZE, 0);
         if (nrecv == -1)
         {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
               pgagroal_log_error("receive_handler: recv\n");
            }
            break;
         }
         else if (nrecv == 0)      /* connection closed */
         {
            ret = EV_CLOSE_FD;
            pgagroal_log_info("Connection closed fd_in=%d fd_out=%d\n", w->fd, w->fd_out);
            break;
         }

         total_recv += nrecv;
      }
      w->data = buf;
      w->size = total_recv;
   }

   w->cb(ev, w, ret);
   return ret;
}

static int
__epoll_accept_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   int listen_fd = w->fd;

   /* TODO: check again if needed:
    *
    * struct sockaddr_in client_addr;
    * socklen_t client_len = sizeof(client_addr);
    */

   while (1)
   {
      w->fd_out = accept(listen_fd, NULL, NULL);
      if (w->fd_out == -1)
      {
         /*
          * NOTE: pgagroal deals with accept returning -1
          */
         if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
         {
            ret = EV_OK;
         }
         else
         {
            ret = EV_ERROR;
         }
         errno = 0;
         break;
      }
      w->cb(ev, w, ret);
   }

   return ret;
}

static int
__epoll_send_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   ssize_t nsent;
   size_t total_sent = 0;
   int fd = w->fd;
   void* buf = w->data;
   size_t buf_len = w->size;

   if (!w->ssl)
   {
      while (total_sent < buf_len)
      {
         nsent = send(fd, buf + total_sent, buf_len - total_sent, 0);
         if (nsent == -1)
         {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
               perror("send");
               ret = EV_ERROR;
               break;
            }
            else if (errno == EPIPE)
            {
               ret = EV_CLOSE_FD;
            }
         }
         else
         {
            total_sent += nsent;
         }
      }
   }

   /*
    * NOTE: Maybe there is an advantage in rearming here since the loop uses non blocking sockets.
    *       But I don't know the case where error occurred and exited the loop and can be recovered.
    *
    *       Example:
    *       if (total_sent < buf_len)
    *            pgagroal_io_send_init(w, fd, cb, buf + total_sent, buf_len, 0);
    */

   return ret;
}
#endif

#if HAVE_KQUEUE

int
__kqueue_loop(struct ev_loop* ev)
{
   int ret;
   int nfds;
   struct kevent events[MAX_EVENTS];
   struct timespec timeout;
   timeout.tv_sec = 0;
   timeout.tv_nsec = 10000000;  /* 10 ms */

   set_running(ev);
   while (is_running(ev))
   {
      nfds = kevent(ev->kqueuefd, NULL, 0, events, MAX_EVENTS, &timeout);

      if (nfds == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }
         pgagroal_log_error("kevent");
         return EV_ERROR;
      }

      if (!is_running(ev))
      {
         break;
      }
      for (int i = 0; i < nfds; i++)
      {
         ret = __kqueue_handler(ev, &events[i]);
         /* Currently closing the main connection fd means that pgagroal loop
          * will stop, so just return an error to the caller. If the caller
          * eventually decides to continue the loop, the
          * caller will have to handle this error.
          */
         if (ret == EV_CLOSE_FD)
         {
            return EV_CLOSE_FD;
         }
         if (ret == EV_ERROR)
         {
            pgagroal_log_fatal("kqueue_handler");
            return EV_ERROR;
         }
      }
   }
   return EV_OK;
}

static int
__kqueue_init(struct ev_loop* ev)
{
   ev->buffer = malloc(sizeof(char) * (MAX_BUFFER_SIZE));
   ev->kqueuefd = kqueue();
   if (ev->kqueuefd == -1)
   {
      pgagroal_log_error("kqueue init error");
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__kqueue_fork(struct ev_loop** parent_loop)
{
   /* TODO: Destroy everything related to loop */
   close((*parent_loop)->kqueuefd);
   return EV_OK;
}

static int
__kqueue_destroy(struct ev_loop* ev)
{
   close(ev->kqueuefd);
   free(ev->buffer);
   free(ev);
   return EV_OK;
}

static int
__kqueue_handler(struct ev_loop* ev, struct kevent* kev)
{
   if (kev->filter == EVFILT_TIMER)
   {
      return __kqueue_periodic_handler(ev, kev);
   }
   else if (kev->filter == EVFILT_SIGNAL)
   {
      return __kqueue_signal_handler(ev, kev);
   }
   else if (kev->filter == EVFILT_READ || kev->filter == EVFILT_WRITE)
   {
      return __kqueue_io_handler(ev, kev);
   }
   else
   {
      pgagroal_log_error("Unknown filter in handler");
      return EV_ERROR;
   }
}

static int
__kqueue_signal_start(struct ev_loop* ev, struct ev_signal* w)
{
   struct kevent kev;

   EV_SET(&kev, w->signum, EVFILT_SIGNAL, EV_ADD, 0, 0, w);
   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent: signal add");
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__kqueue_signal_stop(struct ev_loop* ev, struct ev_signal* w)
{
   struct kevent kev;

   EV_SET(&kev, w->signum, EVFILT_SIGNAL, EV_DELETE, 0, 0, w);
   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent: signal delete");
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__kqueue_signal_handler(struct ev_loop* ev, struct kevent* kev)
{
   struct ev_signal* w = (struct ev_signal*)kev->udata;

   if (w->signum == (int)kev->ident)
   {
      w->cb(ev, w, 0);
      return EV_OK;
   }
   else
   {
      pgagroal_log_error("No handler found for signal %d", (int)kev->ident);
      return EV_ERROR;
   }
}

static int
__kqueue_periodic_init(struct ev_periodic* w, int msec)
{
   w->interval = msec;
   return EV_OK;
}

static int
__kqueue_periodic_start(struct ev_loop* ev, struct ev_periodic* w)
{
   struct kevent kev;
   EV_SET(&kev, (uintptr_t)w, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_USECONDS, w->interval * 1000, w);
   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent: timer add");
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__kqueue_periodic_stop(struct ev_loop* ev, struct ev_periodic* w)
{
   struct kevent kev;
   EV_SET(&kev, (uintptr_t)w, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent: timer delete");
      return EV_ERROR;
   }

   return EV_OK;
}

static int
__kqueue_periodic_handler(struct ev_loop* ev, struct kevent* kev)
{
   struct ev_periodic* w = (struct ev_periodic*)kev->udata;
   w->cb(ev, w, 0);
   return EV_OK;
}

static int
__kqueue_io_start(struct ev_loop* ev, struct ev_io* w)
{
   struct kevent kev;
   int filter;

   switch (w->type)
   {
      case EV_ACCEPT:
      case EV_RECEIVE:
         filter = EVFILT_READ;
         break;
      case EV_SEND:
         filter = EVFILT_WRITE;
         break;
      default:
         pgagroal_log_fatal("%s: unknown event type: %d\n", __func__, w->type);
         return EV_ERROR;
   }

   if (set_non_blocking(w->fd))
   {
      pgagroal_log_fatal("%s: set_non_blocking", __func__);
      return EV_ERROR;
   }

   EV_SET(&kev, w->fd, filter, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, w);

   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("%s: kevent add failed", __func__);
      return EV_ERROR;
   }

   return EV_OK;
}

static int
__kqueue_io_stop(struct ev_loop* ev, struct ev_io* w)
{
   struct kevent kev;
   int filter;

   switch (w->type)
   {
      case EV_ACCEPT:
      case EV_RECEIVE:
         filter = EVFILT_READ;
         break;
      case EV_SEND:
         filter = EVFILT_WRITE;
         break;
      default:
         pgagroal_log_fatal("%s: unknown event type: %d\n", __func__, w->type);
         return EV_ERROR;
   }

   EV_SET(&kev, w->fd, filter, EV_DELETE, 0, 0, NULL);

   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("%s: kevent delete failed", __func__);
      return EV_ERROR;
   }

   return EV_OK;
}

static int
__kqueue_io_handler(struct ev_loop* ev, struct kevent* kev)
{
   struct ev_io* w = (struct ev_io*)kev->udata;
   int ret = EV_OK;

   switch (w->type)
   {
      case EV_ACCEPT:
         ret = __kqueue_accept_handler(ev, w);
         break;
      case EV_SEND:
         ret = __kqueue_send_handler(ev, w);
         break;
      case EV_RECEIVE:
         ret = __kqueue_receive_handler(ev, w);
         break;
      default:
         pgagroal_log_fatal("%s: unknown value for event type %d\n", __func__, w->type);
         ret = EV_ERROR;
         break;
   }

   return ret;
}

static int
__kqueue_receive_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   ssize_t nrecv = 0;
   size_t total_recv = 0;
   void* buf = ev->buffer;

   if (!buf)
   {
      pgagroal_log_error("malloc error");
      return EV_ALLOC_ERROR;
   }

   if (!w->ssl)
   {
      while (1)
      {
         nrecv = recv(w->fd, buf + total_recv, MAX_BUFFER_SIZE - total_recv, 0);
         if (nrecv == -1)
         {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
               break;
            }
            else
            {
               pgagroal_log_error("receive_handler: recv");
               ret = EV_ERROR;
               break;
            }
         }
         else if (nrecv == 0)
         {
            ret = EV_CLOSE_FD;
            // pgagroal_log_trace("Connection closed fd=%d client_fd=%d\n", w->fd, w->fd_out);
            break;
         }

         total_recv += nrecv;

         if (total_recv >= MAX_BUFFER_SIZE)
         {
            pgagroal_log_error("receive_handler: buffer overflow");
            ret = EV_ERROR;
            break;
         }
      }

      w->data = buf;
      w->size = total_recv;
   }

   w->cb(ev, w, ret);
   return ret;
}

static int
__kqueue_accept_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   int listen_fd = w->fd;

   while (1)
   {
      w->fd_out = accept(listen_fd, NULL, NULL);
      if (w->fd_out == -1)
      {
         if (errno == EAGAIN || errno == EWOULDBLOCK)
         {
            ret = EV_OK;
            break;
         }
         else
         {
            pgagroal_log_error("accept_handler: accept");
            ret = EV_ERROR;
            break;
         }
      }
      else
      {
         w->cb(ev, w, ret);
      }
   }

   return ret;
}

static int
__kqueue_send_handler(struct ev_loop* ev, struct ev_io* w)
{
   int ret = EV_OK;
   /* TODO: remove since unused */
   return ret;
}

#endif
