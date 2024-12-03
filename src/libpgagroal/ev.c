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
#include <network.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if HAVE_LINUX
#include <liburing.h>
#include <netdb.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#else
#include <sys/event.h>
#include <sys/types.h>
#include <sys/time.h>
#endif /* HAVE_LINUX */

#define TYPEOF(watcher) watcher->io->type

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
              pgagroal_log_warn("%s: target watcher not found", __func__);      \
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
static int (*loop_start)(struct ev_loop*);
static int (*loop_fork)(struct ev_loop*);
static int (*loop_destroy)(struct ev_loop*);
static void (*loop_break)(struct ev_loop*);

static int io_init(struct ev_io*, int, int, io_cb, void*, int, int);
static int (*io_start)(struct ev_loop*, struct ev_io*);
static int (*io_stop)(struct ev_loop*, struct ev_io*);

static int (*signal_start)(struct ev_loop*, struct ev_signal*);
static int (*signal_stop)(struct ev_loop*, struct ev_signal*);

static int (*periodic_init)(struct ev_periodic*, int);
static int (*periodic_start)(struct ev_loop*, struct ev_periodic*);
static int (*periodic_stop)(struct ev_loop*, struct ev_periodic*);

static bool (*is_running)(struct ev_loop*);
static void (*set_running)(struct ev_loop*);

#if HAVE_LINUX
static int __io_uring_init(struct ev_loop*);
static int __io_uring_destroy(struct ev_loop*);
static int __io_uring_handler(struct ev_loop*, struct io_uring_cqe*);
static int __io_uring_loop(struct ev_loop*);
static int __io_uring_fork(struct ev_loop*);
static int __io_uring_io_start(struct ev_loop*, struct ev_io*);
static int __io_uring_io_stop(struct ev_loop*, struct ev_io*);
static int __io_uring_setup_buffers(struct ev_loop*);
static int __io_uring_setup_more_buffers(struct ev_loop* loop);
static int __io_uring_periodic_init(struct ev_periodic*, int);
static int __io_uring_periodic_start(struct ev_loop*, struct ev_periodic*);
static int __io_uring_periodic_stop(struct ev_loop*, struct ev_periodic*);
static int __io_uring_signal_handler(struct ev_loop*, int);
static int __io_uring_signal_start(struct ev_loop*, struct ev_signal*);
static int __io_uring_signal_stop(struct ev_loop*, struct ev_signal*);
static int __io_uring_receive_handler(struct ev_loop*, struct ev_io*, struct io_uring_cqe*, void**, bool);
static int __io_uring_send_handler(struct ev_loop*, struct ev_io*, struct io_uring_cqe*);
static int __io_uring_accept_handler(struct ev_loop*, struct ev_io*, struct io_uring_cqe*);
static int __io_uring_periodic_handler(struct ev_loop*, struct ev_periodic*);

static int __epoll_init(struct ev_loop*);
static int __epoll_destroy(struct ev_loop*);
static int __epoll_handler(struct ev_loop*, void*);
static int __epoll_loop(struct ev_loop*);
static int __epoll_fork(struct ev_loop*);
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
#else
static int __kqueue_init(struct ev_loop*);
static int __kqueue_destroy(struct ev_loop*);
static int __kqueue_handler(struct ev_loop*, struct kevent*);
static int __kqueue_loop(struct ev_loop*);
static int __kqueue_fork(struct ev_loop*);
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
#endif /* HAVE_LINUX */

/* context globals */

static bool multithreading = false;    /* Enable multithreading for a loop */

#ifdef HAVE_LINUX
static struct io_uring_params params; /* io_uring argument params */
static int entries;            /* io_uring entries flag */
static bool use_huge;          /* io_uring use_huge flag */
static int buf_size;           /* Size of the ring-mapped buffers */
static int buf_count;          /* Number of ring-mapped buffers */
static int br_mask;            /* Buffer ring mask value */

static int epoll_flags;               /* Flags for epoll instance creation */
#else
static int kqueue_flags;              /* Flags for kqueue instance creation */
#endif /* HAVE_LINUX */

static inline bool
_is_running(struct ev_loop* loop)
{
   return loop->running;
}

static inline bool
_is_running_atomic(struct ev_loop* loop)
{
   return atomic_load(&loop->atomic_running);
}

static inline void
_set_running(struct ev_loop* loop)
{
   loop->running = true;
}
static inline void
_set_running_atomic(struct ev_loop* loop)
{
   atomic_store(&loop->atomic_running, true);
}

static inline void
_break(struct ev_loop* loop)
{
   loop->running = false;
}
static inline void
_break_atomic(struct ev_loop* loop)
{
   atomic_store(&loop->atomic_running, false);
}

static int
setup_ops(struct ev_loop* loop)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   is_running = multithreading ? _is_running_atomic : _is_running;
   set_running = multithreading ? _set_running_atomic: _set_running;
   loop_break = multithreading ? _break_atomic: _break;

#if HAVE_LINUX
   if (config->ev_backend == EV_BACKEND_IO_URING)
   {
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
      return EV_OK;
   }
   else if (config->ev_backend == EV_BACKEND_EPOLL)
   {
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
      return EV_OK;
   }
#else
   if (config->ev_backend == EV_BACKEND_KQUEUE)
   {
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
      return EV_OK;
   }
#endif /* HAVE_LINUX */
   return EV_ERROR;
}

static void
sigchld_handler(struct ev_loop* loop, struct ev_signal* w, int sig)
{
   while (waitpid(-1, NULL, WNOHANG) > 0)
      ;
}

struct ev_loop*
pgagroal_ev_init(void)
{
   struct ev_loop* loop;
   static ev_signal w = {
      .type = EV_SIGNAL,
      .signum = SIGCHLD,
      .cb = sigchld_handler,
      .next = NULL,
   };

   static bool context_is_set = false;

   loop = calloc(1, sizeof(struct ev_loop));
   sigemptyset(&loop->sigset);

   if (!context_is_set)
   {

#if HAVE_LINUX
      /* io_uring context */
      entries = 32;
      params.cq_entries = 64;
      params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_CLAMP | IORING_SETUP_CQSIZE;
      buf_count = BUFFER_COUNT;
      buf_size = DEFAULT_BUFFER_SIZE;
      br_mask = (buf_count - 1);

      /* epoll context */
      epoll_flags = 0;
#else
      /* kqueue context */
      kqueue_flags = 0;
#endif /* HAVE_LINUX */

      if (setup_ops(loop))
      {
         pgagroal_log_fatal("pgagroal: Failed to event backend operations");
         goto error;
      }

      if (loop_init(loop))
      {
         pgagroal_log_fatal("pgagroal: Failed to initiate loop");
         goto error;
      }

      /* handle with SIGCHLD if the main_loop */
      pgagroal_ev_signal_start(loop, &w);

      context_is_set = true;
   }
   else if (loop_init(loop))
   {
      pgagroal_log_fatal("pgagroal: Failed to initiate loop");
      goto error;
   }

   return loop;

error:
   free(loop);
   return NULL;
}

int
pgagroal_ev_loop(struct ev_loop* loop)
{
   return loop_start(loop);
}

int
pgagroal_ev_fork(struct ev_loop* loop)
{
   if (sigprocmask(SIG_UNBLOCK, &loop->sigset, NULL) == -1)
   {
      pgagroal_log_fatal("sigprocmask");
      exit(1);
   }
   /* no need to empty sigset */
   return loop_fork(loop);
}

int
pgagroal_ev_loop_destroy(struct ev_loop* loop)
{
   int ret;
   if (!loop)
   {
      return EV_OK;
   }
   ret = loop_destroy(loop);
   free(loop);
   return ret;
}

void
pgagroal_ev_loop_break(struct ev_loop* loop)
{
   loop_break(loop);
}

bool
pgagroal_ev_loop_is_running(struct ev_loop* loop)
{
   return loop->running;
}

bool
pgagroal_ev_atomic_loop_is_running(struct ev_loop* loop)
{
   return atomic_load(&loop->atomic_running);
}

int
pgagroal_ev_io_accept_init(struct ev_io* w, int fd, io_cb cb)
{
   return io_init(w, fd, EV_ACCEPT, cb, NULL, 0, -1);
}

int
pgagroal_ev_io_send_init(struct ev_io* w, int fd, io_cb cb, void* buf, int buf_len)
{
   return io_init(w, fd, EV_SEND, cb, buf, buf_len, -1);
}

int
pgagroal_ev_io_receive_init(struct ev_io* w, int fd, io_cb cb)
{
   return io_init(w, fd, EV_RECEIVE, cb, NULL, 0, -1);
}

int
pgagroal_ev_io_start(struct ev_loop* loop, struct ev_io* w)
{
   list_add(w, loop->ihead.next);
   return io_start(loop, w);
}

int
pgagroal_ev_io_stop(struct ev_loop* loop, struct ev_io* target)
{
   int ret = EV_OK;
   struct ev_io** w;
   if (!loop)
   {
      pgagroal_log_debug("loop is NULL");
      return EV_OK;
   }
   if (!target)
   {
      pgagroal_log_fatal("target is NULL");
      exit(1);
   }
   io_stop(loop, target);
   list_delete(w, &loop->ihead.next, target, ret);
   return ret;
}

int
pgagroal_ev_signal_init(struct ev_signal* w, signal_cb cb, int signum)
{
   w->type = EV_SIGNAL;
   w->signum = signum;
   w->cb = cb;
   w->next = NULL;
   return EV_OK;
}

int
pgagroal_ev_signal_start(struct ev_loop* loop, struct ev_signal* w)
{
   sigaddset(&loop->sigset, w->signum);
   if (sigprocmask(SIG_BLOCK, &loop->sigset, NULL) == -1)
   {
      pgagroal_log_fatal("sigprocmask");
      exit(1);
   }
   signal_start(loop, w);
   list_add(w, loop->shead.next);
   return EV_OK;
}

int
pgagroal_ev_signal_stop(struct ev_loop* loop, struct ev_signal* target)
{
   int ret = EV_OK;
   struct ev_signal** w;

   if (!target)
   {
      pgagroal_log_fatal("target is NULL");
      exit(1);
   }

   sigdelset(&loop->sigset, target->signum);

   if (pthread_sigmask(SIG_UNBLOCK, &loop->sigset, NULL) == -1)
   {
      pgagroal_log_error("pthread_sigmask");
      return EV_ERROR;
   }

   signal_stop(loop, target);

   list_delete(w, &loop->shead.next, target, ret);

   return ret;
}

int
pgagroal_ev_periodic_init(struct ev_periodic* w, periodic_cb cb, int msec)
{
   if (periodic_init(w, msec))
   {
      pgagroal_log_fatal("periodic_init");
      exit(1);
   }
   w->type = EV_PERIODIC;
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
pgagroal_ev_periodic_stop(struct ev_loop* loop, struct ev_periodic* target)
{
   int ret;
   struct ev_periodic** w;
   if (!target)
   {
      pgagroal_log_error("null pointer provided to stop");
      return EV_ERROR;
   }
   ret = periodic_stop(loop, target);
   list_delete(w, &loop->phead.next, target, ret);
   return ret;
}

static int
io_init(struct ev_io* w, int fd, int event, io_cb cb, void* data, int size, int rsvd)
{
   w->type = event;
   w->fd = fd;
   w->cb = cb;
   w->data = data;
   w->size = size;
   return EV_OK;
}

#if HAVE_LINUX
static inline struct io_uring_sqe*
__io_uring_get_sqe(struct ev_loop* loop)
{
   struct io_uring* ring = &loop->ring;
   struct io_uring_sqe* sqe;
   /* this loop is necessary if SQPOLL is being used */
   do
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

static inline void __attribute__((unused))
__io_uring_rearm_receive(struct ev_loop* loop, struct ev_io* w)
{
   struct io_uring_sqe* sqe = __io_uring_get_sqe(loop);
   io_uring_sqe_set_data(sqe, w);
   io_uring_prep_recv_multishot(sqe, w->fd, NULL, 0, 0);
   sqe->flags |= IOSQE_BUFFER_SELECT;
   sqe->buf_group = 0;
}

static inline int __attribute__((unused))
__io_uring_replenish_buffers(struct ev_loop* loop, struct io_buf_ring* br, int bid_start, int bid_end)
{
   int count;
   if (bid_end >= bid_start)
   {
      count = (bid_end - bid_start);
   }
   else
   {
      count = (bid_end + buf_count - bid_start);
   }
   for (int i = bid_start; i != bid_end; i = (i + 1) & (buf_count - 1))
   {
      io_uring_buf_ring_add(br->br, (void*)br->br->bufs[i].addr, buf_size, i, br_mask, 0);
   }
   io_uring_buf_ring_advance(br->br, count);
   return EV_OK;
}

static int
__io_uring_init(struct ev_loop* loop)
{
   int ret;
   ret = io_uring_queue_init_params(entries, &loop->ring, &params);
   if (ret)
   {
      pgagroal_log_fatal("io_uring_queue_init_params: %s", strerror(-ret));
      exit(1);
   }
   ret = __io_uring_setup_buffers(loop);
   if (ret)
   {
      pgagroal_log_fatal("__io_uring_setup_buffers error: %s", strerror(-ret));
      exit(1);
   }
   ret = io_uring_ring_dontfork(&loop->ring);
   if (ret)
   {
      pgagroal_log_fatal("error on io_uring_ring_dontfork: %s", strerror(-ret));
      exit(1);
   }
   return EV_OK;
}

static int
__io_uring_destroy(struct ev_loop* loop)
{
   const int bgid = 0; /* const for now */
   struct io_buf_ring* br = &loop->br;
   if (io_uring_free_buf_ring(&loop->ring, br->br, buf_count, bgid))
   {
      pgagroal_log_fatal("ev: io_uring_free_buf_ring (%s)", strerror(errno));
      exit(1);
   }
   free(br->buf);
   io_uring_queue_exit(&loop->ring);
   return EV_OK;
}

static int
__io_uring_io_start(struct ev_loop* loop, struct ev_io* w)
{
   struct io_uring_sqe* sqe = __io_uring_get_sqe(loop);
   io_uring_sqe_set_data(sqe, w);
   switch (w->type)
   {
      case EV_ACCEPT:
         io_uring_prep_multishot_accept(sqe, w->fd, NULL, NULL, 0);
         break;
      case EV_RECEIVE:
         io_uring_prep_recv(sqe, w->fd, loop->br.buf, buf_size, 0);
         if (0)
         {
            sqe->ioprio |= IORING_RECV_MULTISHOT;
         }

         sqe->flags |= IOSQE_BUFFER_SELECT;
         sqe->buf_group = 0;
         break;
      case EV_SEND:
         io_uring_prep_send(sqe, w->fd, w->data, w->size, MSG_WAITALL | MSG_NOSIGNAL); /* TODO: flags */
         break;
      default:
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
   }
   return EV_OK;
}

static int
__io_uring_io_stop(struct ev_loop* loop, struct ev_io* target)
{
   int ret = EV_OK;
   struct io_uring_sqe* sqe;
   sqe = io_uring_get_sqe(&loop->ring);
   io_uring_prep_cancel64(sqe, (uint64_t)target, 0); /* TODO: flags? */
   return ret;
}

static int
__io_uring_signal_start(struct ev_loop* loop, struct ev_signal* w)
{
   return EV_OK;
}

static int
__io_uring_signal_stop(struct ev_loop* loop, struct ev_signal* w)
{
   return EV_OK;
}

static int
__io_uring_periodic_init(struct ev_periodic* w, int msec)
{
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
__io_uring_loop(struct ev_loop* loop)
{
   int ret;
   int signum;
   int events;
   int to_wait = 1; /* wait for any 1 */
   unsigned int head;
   struct io_uring_cqe* cqe = NULL;
   struct __kernel_timespec* ts = NULL;
   struct __kernel_timespec idle_ts = {
      .tv_sec = 0,
      .tv_nsec = 10000000LL
   };
   struct timespec timeout = {
      .tv_sec = 0,
      .tv_nsec = 0,
   };

   set_running(loop);
   while (is_running(loop))
   {
      ts = &idle_ts;
      io_uring_submit_and_wait_timeout(&loop->ring, &cqe, to_wait, ts, NULL);

      /* Good idea to leave here to see what happens */
      if (*loop->ring.cq.koverflow)
      {
         pgagroal_log_error("io_uring overflow %u", *loop->ring.cq.koverflow);
         exit(EXIT_FAILURE);
      }
      if (*loop->ring.sq.kflags & IORING_SQ_CQ_OVERFLOW)
      {
         pgagroal_log_error("io_uring overflow");
         exit(EXIT_FAILURE);
      }

      /* Check for signals before iterating over cqes */
      signum = sigtimedwait(&loop->sigset, NULL, &timeout);
      if (signum > 0)
      {
         ret = __io_uring_signal_handler(loop, signum);

         if (ret == EV_ERROR)
         {
            pgagroal_log_error("Signal handling error");
            return EV_ERROR;
         }
         if (!is_running(loop))
         {
            break;
         }
      }

      events = 0;
      io_uring_for_each_cqe(&(loop->ring), head, cqe)
      {
         ret = __io_uring_handler(loop, cqe);
         events++;
      }
      if (events)
      {
         io_uring_cq_advance(&loop->ring, events);  /* batch marking as seen */
      }

      /* TODO: housekeeping ? */

   }
   return ret;
}

static int
__io_uring_fork(struct ev_loop* loop)
{
   return EV_OK;
}

static int
__io_uring_handler(struct ev_loop* loop, struct io_uring_cqe* cqe)
{
   int ret = EV_OK;
   ev_watcher w;
   w.io = (ev_io*)io_uring_cqe_get_data(cqe);

   void* buf;

   /*
    * Cancelled requests will trigger the handler, but have NULL data.
    */
   if (!w.io)
   {
      return EV_OK;
   }

   /* io handler */
   switch (w.io->type)
   {
      case EV_PERIODIC:
         return __io_uring_periodic_handler(loop, w.periodic);
      case EV_ACCEPT:
         return __io_uring_accept_handler(loop, w.io, cqe);
      case EV_SEND:
         return __io_uring_send_handler(loop, w.io, cqe);
      case EV_RECEIVE:
retry:
         ret = __io_uring_receive_handler(loop, w.io, cqe, &buf, false);
         switch (ret)
         {
            case EV_CONNECTION_CLOSED: /* connection closed */
               break;
            case EV_ERROR:
               break;
            case EV_REPLENISH_BUFFERS:
               if (__io_uring_setup_more_buffers(loop))
               {
                  return EV_ERROR;
               }
               goto retry;
               break;
         }
         break;
      default:
         pgagroal_log_fatal("unknown event type: %d", w.io->type);
         exit(1);
   }
   return ret;
}

static int
__io_uring_periodic_handler(struct ev_loop* loop, struct ev_periodic* w)
{
   w->cb(loop, w, 0);
   return EV_OK;
}

static int
__io_uring_accept_handler(struct ev_loop* loop, struct ev_io* w, struct io_uring_cqe* cqe)
{
   w->client_fd = cqe->res;
   w->cb(loop, w, EV_OK);
   return EV_OK;
}

static int
__io_uring_send_handler(struct ev_loop* loop, struct ev_io* w, struct io_uring_cqe* cqe)
{
   struct io_buf_ring* br = &loop->br;
   const int bid = 0;
   const int cnt = 1;

   io_uring_buf_ring_add(br->br, (void*) br->br->bufs[bid].addr, buf_size, 0, br_mask, bid);
   io_uring_buf_ring_advance(br->br, cnt);

   struct io_uring_sqe* sqe = __io_uring_get_sqe(loop);
   io_uring_sqe_set_data(sqe, w);
   io_uring_prep_recv(sqe, w->fd, NULL, 0, 0);
   sqe->flags |= IOSQE_BUFFER_SELECT | MSG_WAITALL;
   return EV_OK;
}

static int
__io_uring_signal_handler(struct ev_loop* loop, int signo)
{
   struct ev_signal* w;
   for_each(w, loop->shead.next)
   {
      if (w->signum == signo)
      {
         w->cb(loop, w, 0);
         return EV_OK;
      }
   }
   return EV_ERROR;
}

static int
__io_uring_receive_handler(struct ev_loop* loop, struct ev_io* w, struct io_uring_cqe* cqe, void** _unused, bool __unused)
{
   int bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
   struct io_buf_ring* br = &loop->br;
   int total_in_bytes = cqe->res;
   int cnt = 1;

   if (cqe->res == -ENOBUFS)
   {
      pgagroal_log_warn("ev: Not enough buffers");
      exit(1);
      /* TODO return EV_REPLENISH_BUFFERS; */
   }

   if (!(cqe->flags & IORING_CQE_F_BUFFER) && !(cqe->res))
   {
      pgagroal_log_debug("ev: Connection closed");
      w->data = NULL;
      w->size = 0;
      w->cb(loop, w, EV_OK);
      return EV_OK;
   }

   w->data = br->buf + (bid * buf_size);
   w->size = total_in_bytes;
   w->cb(loop, w, EV_OK);

   // struct io_uring_sqe* sqe = __io_uring_get_sqe(loop);
   // io_uring_sqe_set_data(sqe, w);
   // io_uring_prep_send(sqe, w->sendto->fd, w->data, w->size, 0);

   io_uring_buf_ring_add(br->br, w->data, buf_size, bid, br_mask, bid);
   io_uring_buf_ring_advance(br->br, cnt);

   __io_uring_io_start(loop, w);

   return EV_OK;
}

static int __attribute__((unused))
__io_uring_receive_multishot_handler(struct ev_loop* loop, struct ev_io* w, struct io_uring_cqe* cqe, void** unused, bool is_proxy)
{
   struct io_buf_ring* br = &loop->br;
   int bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
   int total_in_bytes = cqe->res;
   int cnt = 1;

   if (cqe->res == -ENOBUFS)
   {
      pgagroal_log_warn("ev: Not enough buffers");
      return EV_REPLENISH_BUFFERS;
   }

   if (!(cqe->flags & IORING_CQE_F_BUFFER) && !(cqe->res))
   {
      pgagroal_log_debug("ev: Connection closed");
      return EV_CONNECTION_CLOSED;
   }
   else if (!(cqe->flags & IORING_CQE_F_MORE))
   {
      /* do not rearm receive. In fact, disarm anything so pgagroal can deal with
       * read / write from sockets
       */
      pgagroal_log_debug("ev: Transaction likely cancelled");
      w->data = NULL;
      w->size = 0;
      w->cb(loop, w, EV_ERROR);
      return EV_CONNECTION_CLOSED;
   }

   w->data = br->buf + (bid * buf_size);
   w->size = total_in_bytes;
   w->cb(loop, w, EV_OK);
   io_uring_buf_ring_add(br->br, w->data, buf_size, bid, br_mask, bid);
   io_uring_buf_ring_advance(br->br, cnt);

   return EV_OK;
}

static int
__io_uring_setup_buffers(struct ev_loop* loop)
{
   int ret;
   int br_bgid = 0;
   int br_flags = 0;
   void* ptr;

   struct io_buf_ring* br = &loop->br;
   if (use_huge)
   {
      pgagroal_log_fatal("io_uring use_huge not implemented yet");
      exit(1);
   }
   if (posix_memalign(&br->buf, ALIGNMENT, buf_count * buf_size))
   {
      pgagroal_log_fatal("posix_memalign");
      exit(1);
   }

   br->br = io_uring_setup_buf_ring(&loop->ring, buf_count, br_bgid, br_flags, &ret);
   if (!br->br)
   {
      pgagroal_log_fatal("buffer ring register failed %d", strerror(-ret));
      exit(1);
   }

   ptr = br->buf;
   for (int i = 0; i < buf_count; i++)
   {
      io_uring_buf_ring_add(br->br, ptr, buf_size, i, br_mask, i);
      ptr += buf_size;
   }
   io_uring_buf_ring_advance(br->br, buf_count);

   return EV_OK;
}

static int
__io_uring_setup_more_buffers(struct ev_loop* loop)
{
   int ret = EV_OK;
   int br_bgid = 0;
   int br_flags = 0;
   void* ptr;
   exit(1);

   struct io_buf_ring* br = &loop->br;
   if (use_huge)
   {
      pgagroal_log_fatal("io_uring use_huge not implemented yet");
      exit(1);
   }
   if (posix_memalign(&br->buf, ALIGNMENT, buf_count * buf_size))
   {
      pgagroal_log_fatal("posix_memalign");
      exit(1);
   }

   br->br = io_uring_setup_buf_ring(&loop->ring, buf_count, br_bgid, br_flags, &ret);
   if (!br->br)
   {
      pgagroal_log_fatal("buffer ring register failed %d", strerror(-ret));
      exit(1);
   }

   ptr = br->buf;
   for (int i = 0; i < buf_count; i++)
   {
      io_uring_buf_ring_add(br->br, ptr, buf_size, i, br_mask, i);
      ptr += buf_size;
   }
   io_uring_buf_ring_advance(br->br, buf_count);

   return EV_OK;
}

void
_next_bid(struct ev_loop* loop, int* bid)
{
   *bid = (*bid + 1) % buf_count;
}

int
__epoll_loop(struct ev_loop* loop)
{
   int ret = EV_OK;
   int nfds;
   struct epoll_event events[MAX_EVENTS];
#if HAVE_EPOLL_PWAIT2
   struct timespec timeout_ts = {
      .tv_sec = 0,
      .tv_nsec = 10000000LL,
   };
#else
   int timeout = 10000LL; /* ms */
#endif
   struct epoll_event ev = {
      .events = EPOLLIN | EPOLLET,
      .data.fd = signalfd(-1, &loop->sigset, 0),
   };
   if (ev.data.fd == -1)
   {
      pgagroal_log_fatal("signalfd");
      exit(1);
   }
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, ev.data.fd, &ev) == -1)
   {
      pgagroal_log_fatal("ev: epoll_ctl (%s)", strerror(errno));
      exit(1);
   }

   set_running(loop);
   while (is_running(loop))
   {
#if HAVE_EPOLL_PWAIT2
      nfds = epoll_pwait2(loop->epollfd, events, MAX_EVENTS, &timeout_ts, &loop->sigset);
#else
      nfds = epoll_pwait(loop->epollfd, events, MAX_EVENTS, timeout, &loop->sigset);
#endif

      for (int i = 0; i < nfds; i++)
      {
         if (events[i].data.fd == ev.data.fd)
         {
            ret = __epoll_signal_handler(loop);
         }
         else
         {
            ret = __epoll_handler(loop, (void*)events[i].data.u64);
         }
      }
   }
   return ret;
}

static int
__epoll_init(struct ev_loop* loop)
{
   loop->epollfd = epoll_create1(epoll_flags);
   if (loop->epollfd == -1)
   {
      pgagroal_log_fatal("epoll_init");
      exit(1);
   }
   return EV_OK;
}

static int
__epoll_fork(struct ev_loop* loop)
{

   close(loop->epollfd);
   return EV_OK;
}

static int
__epoll_destroy(struct ev_loop* loop)
{
   close(loop->epollfd);
   return EV_OK;
}

static int
__epoll_handler(struct ev_loop* loop, void* wp)
{
   struct ev_periodic* w = (struct ev_periodic*)wp;
   if (w->type == EV_PERIODIC)
   {
      return __epoll_periodic_handler(loop, (struct ev_periodic*)w);
   }
   return __epoll_io_handler(loop, (struct ev_io*)w);
}

static int
__epoll_signal_start(struct ev_loop* loop, struct ev_signal* w)
{
   return EV_OK;
}

static int
__epoll_signal_stop(struct ev_loop* loop, struct ev_signal* w)
{
   return EV_OK;
}

static int
__epoll_signal_handler(struct ev_loop* loop)
{
   struct ev_signal* w;
   siginfo_t siginfo;
   int signo;
   signo = sigwaitinfo(&loop->sigset, &siginfo);
   if (signo == -1)
   {
      pgagroal_log_error("sigwaitinfo");
      return EV_ERROR;
   }

   for_each(w, loop->shead.next)
   {
      if (w->signum == signo)
      {
         w->cb(loop, w, 0);
         return EV_OK;
      }
   }

   pgagroal_log_error("No handler found for signal %d", signo);
   return EV_ERROR;
}

static int
__epoll_periodic_init(struct ev_periodic* w, int msec)
{
   struct timespec now;
   struct itimerspec new_value;

   if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
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
      pgagroal_log_error("timerfd_create");
      return EV_ERROR;
   }

   if (timerfd_settime(w->fd, 0, &new_value, NULL) == -1)
   {
      pgagroal_log_error("timerfd_settime");
      close(w->fd);
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__epoll_periodic_start(struct ev_loop* loop, struct ev_periodic* w)
{
   struct epoll_event event;
   event.events = EPOLLIN;
   event.data.u64 = (uint64_t)w;
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, w->fd, &event) == -1)
   {
      pgagroal_log_fatal("ev: epoll_ctl (%s)", strerror(errno));
      exit(1);
   }
   return EV_OK;
}

static int
__epoll_periodic_stop(struct ev_loop* loop, struct ev_periodic* w)
{
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, w->fd, NULL) == -1)
   {
      pgagroal_log_fatal("%s: epoll_ctl (%s)", strerror(errno));
      exit(1);
   }
   return EV_OK;
}

static int
__epoll_periodic_handler(struct ev_loop* loop, struct ev_periodic* w)
{
   uint64_t exp;
   int nread = read(w->fd, &exp, sizeof(uint64_t));
   if (nread != sizeof(uint64_t))
   {
      pgagroal_log_error("periodic_handler: read");
      return EV_ERROR;
   }
   w->cb(loop, w, 0);
   return EV_OK;
}

static int
__epoll_io_start(struct ev_loop* loop, struct ev_io* w)
{
   struct epoll_event event;

   event.data.u64 = (uintptr_t)w;

   switch (w->type)
   {
      case EV_ACCEPT:
         event.events = EPOLLIN;
         break;
      case EV_RECEIVE:
         pgagroal_socket_nonblocking(w->fd, true);
         event.events = EPOLLIN; /* TODO: | EPOLLET; */
         break;
      case EV_SEND:
         event.events = EPOLLOUT;
         break;
      default:
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
   }

   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, w->fd, &event) == -1)
   {
      pgagroal_log_fatal("ev: epoll_ctl (%s)", strerror(errno));
      exit(1);
   }

   return EV_OK;
}

static int
__epoll_io_stop(struct ev_loop* ev, struct ev_io* target)
{
   if (epoll_ctl(ev->epollfd, EPOLL_CTL_DEL, target->fd, NULL) == -1)
   {
      if (errno == EBADF || errno == ENOENT || errno == EINVAL)
      {
         pgagroal_log_debug("ev: epoll_ctl failed (%s)", strerror(errno));
      }
      else
      {
         pgagroal_log_fatal("ev: epoll_ctl (%s)", strerror(errno));
         exit(1);
      }
   }
   return EV_OK;
}

static int
__epoll_io_handler(struct ev_loop* loop, struct ev_io* w)
{
   switch (w->type)
   {
      case EV_ACCEPT:
         return __epoll_accept_handler(loop, w);
      case EV_SEND:
         return __epoll_send_handler(loop, w);
      case EV_RECEIVE:
         return __epoll_receive_handler(loop, w);
      default:
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
   }
}

static int
__epoll_accept_handler(struct ev_loop* loop, struct ev_io* w)
{
   int ret = EV_OK;
   int listen_fd = w->fd;
   int client_fd;

   client_fd = accept(listen_fd, NULL, NULL);
   if (client_fd == -1)
   {
      if (!(errno == EAGAIN) && !(errno == EWOULDBLOCK))
      {
         ret = EV_ERROR;
      }
   }
   else
   {
      pgagroal_socket_nonblocking(client_fd, true);
      w->client_fd = client_fd;
      w->cb(loop, w, ret);
   }

   return ret;
}

static int
__epoll_receive_handler(struct ev_loop* loop, struct ev_io* w)
{
   int ret = EV_OK;
   w->cb(loop, w, ret);
   return ret;
}

static int
__epoll_send_handler(struct ev_loop* loop, struct ev_io* w)
{
   int ret = EV_OK;
   w->cb(loop, w, ret);
   return ret;
}

#else

int
__kqueue_loop(struct ev_loop* ev)
{
   int ret = EV_OK;
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
      }
   }
   return ret;
}

static int
__kqueue_init(struct ev_loop* ev)
{
   ev->kqueuefd = kqueue();
   if (ev->kqueuefd == -1)
   {
      perror("kqueue");
      pgagroal_log_fatal("kqueue init error");
      exit(1);
   }
   return EV_OK;
}

static int
__kqueue_fork(struct ev_loop* loop)
{
   close(loop->kqueuefd);
   return EV_OK;
}

static int
__kqueue_destroy(struct ev_loop* ev)
{
   close(ev->kqueuefd);
   return EV_OK;
}

static int
__kqueue_handler(struct ev_loop* ev, struct kevent* kev)
{
   switch (kev->filter)
   {
      case EVFILT_TIMER:
         return __kqueue_periodic_handler(ev, kev);
      case EVFILT_SIGNAL:
         return __kqueue_signal_handler(ev, kev);
      case EVFILT_READ:
      case EVFILT_WRITE:
         return __kqueue_io_handler(ev, kev);
      default:
         pgagroal_log_fatal("ev: Unknown filter in handler");
         exit(1);
   }
}

static int
__kqueue_signal_start(struct ev_loop* loop, struct ev_signal* w)
{
   struct kevent kev;

   pgagroal_log_debug("ev: starting signal %d", w->signum);
   EV_SET(&kev, w->signum, EVFILT_SIGNAL, EV_ADD, 0, 0, w);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_fatal("ev: kevent (%s)", strerror(errno));
      exit(1);
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
      pgagroal_log_fatal("ev: kevent (%s)", strerror(errno));
      exit(1);
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
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
   }

   pgagroal_socket_nonblocking(w->fd, true);

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
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
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
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
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
         nrecv = recv(w->fd, buf + total_recv, DEFAULT_BUFFER_SIZE - total_recv, 0);
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
            ret = EV_CONNECTION_CLOSED;
            break;
         }

         total_recv += nrecv;

         if (total_recv >= DEFAULT_BUFFER_SIZE)
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
      w->client_fd = accept(listen_fd, NULL, NULL);
      if (w->client_fd == -1)
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

#endif /* HAVE_LINUX */
