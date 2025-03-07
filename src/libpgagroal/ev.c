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
#include <message.h>
#include <memory.h>
#include <network.h>
#include <pgagroal.h>
#include <shmem.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if HAVE_LINUX
#include <liburing.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#endif /* HAVE_LINUX */

static int (*loop_init)(struct event_loop*);
static int (*loop_start)(struct event_loop*);
static int (*loop_fork)(struct event_loop*);
static int (*loop_destroy)(struct event_loop*);

static int (*io_start)(struct event_loop*, struct io_watcher*);
static int (*io_stop)(struct event_loop*, struct io_watcher*);

static void signal_handler(int signum, siginfo_t* info, void* p);

static int (*periodic_init)(struct periodic_watcher*, int);
static int (*periodic_start)(struct event_loop*, struct periodic_watcher*);
static int (*periodic_stop)(struct event_loop*, struct periodic_watcher*);

#if HAVE_LINUX

static int ev_io_uring_init(struct event_loop*);
static int ev_io_uring_destroy(struct event_loop*);
static int ev_io_uring_handler(struct event_loop*, struct io_uring_cqe*);
static int ev_io_uring_loop(struct event_loop*);
static int ev_io_uring_fork(struct event_loop*);
static int ev_io_uring_io_start(struct event_loop*, struct io_watcher*);
static int ev_io_uring_io_stop(struct event_loop*, struct io_watcher*);
static int ev_io_uring_periodic_init(struct periodic_watcher*, int);
static int ev_io_uring_periodic_start(struct event_loop*, struct periodic_watcher*);
static int ev_io_uring_periodic_stop(struct event_loop*, struct periodic_watcher*);
static int ev_io_uring_receive_handler(struct event_loop*, struct io_watcher*, struct io_uring_cqe*, void**, bool);
static int ev_io_uring_accept_handler(struct event_loop*, struct io_watcher*, struct io_uring_cqe*);
static int ev_io_uring_periodic_handler(struct event_loop*, struct periodic_watcher*);

static int ev_epoll_init(struct event_loop*);
static int ev_epoll_destroy(struct event_loop*);
static int ev_epoll_handler(struct event_loop*, void*);
static int ev_epoll_loop(struct event_loop*);
static int ev_epoll_fork(struct event_loop*);
static int ev_epoll_io_start(struct event_loop*, struct io_watcher*);
static int ev_epoll_io_stop(struct event_loop*, struct io_watcher*);
static int ev_epoll_io_handler(struct event_loop*, struct io_watcher*);
static int ev_epoll_send_handler(struct event_loop*, struct io_watcher*);
static int ev_epoll_accept_handler(struct event_loop*, struct io_watcher*);
static int ev_epoll_receive_handler(struct event_loop*, struct io_watcher*);
static int ev_epoll_periodic_init(struct periodic_watcher*, int);
static int ev_epoll_periodic_start(struct event_loop*, struct periodic_watcher*);
static int ev_epoll_periodic_stop(struct event_loop*, struct periodic_watcher*);
static int ev_epoll_periodic_handler(struct event_loop*, struct periodic_watcher*);

#else

static int __kqueue_init(struct ev_loop*);
static int __kqueue_destroy(struct ev_loop*);
static int __kqueue_handler(struct ev_loop*, struct kevent*);
static int __kqueue_loop(struct ev_loop*);
static int __kqueue_fork(struct ev_loop*);
static int __kqueue_io_start(struct ev_loop*, struct struct io_watcher*);
static int __kqueue_io_stop(struct ev_loop*, struct struct io_watcher*);
static int __kqueue_io_handler(struct ev_loop*, struct kevent*);
static int __kqueue_send_handler(struct ev_loop*, struct struct io_watcher*);
static int __kqueue_accept_handler(struct ev_loop*, struct struct io_watcher*);
static int __kqueue_receive_handler(struct ev_loop*, struct struct io_watcher*);
static int __kqueue_periodic_init(struct struct periodic_watcher*, int);
static int __kqueue_periodic_start(struct ev_loop*, struct struct periodic_watcher*);
static int __kqueue_periodic_stop(struct ev_loop*, struct struct periodic_watcher*);
static int __kqueue_periodic_handler(struct ev_loop*, struct kevent*);
static int __kqueue_signal_stop(struct ev_loop*, struct ev_signal*);
static int __kqueue_signal_handler(struct ev_loop*, struct kevent*);
static int __kqueue_signal_start(struct ev_loop*, struct ev_signal*);

#endif /* HAVE_LINUX */

/* context globals */

static struct event_loop* loop;
static struct signal_watcher *signal_watchers[_NSIG] = {0};

#ifdef HAVE_LINUX

static struct io_uring_params params; /* io_uring argument params */
static int entries;                   /* io_uring entries flag */
static bool use_huge;                 /* io_uring use_huge flag */
static bool sq_poll;                  /* io_uring sq_poll flag */
static bool fast_poll;                /* io_uring fast_poll flag */
static int buf_size;                  /* Size of the ring-mapped buffers */
static int buf_count;                 /* Number of ring-mapped buffers */
static int br_mask;                   /* Buffer ring mask value */
static bool mshot;

static int epoll_flags;               /* Flags for epoll instance creation */

#else

static int kqueue_flags;              /* Flags for kqueue instance creation */

#endif /* HAVE_LINUX */

static int
setup_ops(struct event_loop* loop)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

#if HAVE_LINUX
   if (config->ev_backend == EV_BACKEND_IO_URING)
   {
      loop_init = ev_io_uring_init;
      loop_fork = ev_io_uring_fork;
      loop_destroy = ev_io_uring_destroy;
      loop_start = ev_io_uring_loop;
      io_start = ev_io_uring_io_start;
      io_stop = ev_io_uring_io_stop;
      periodic_init = ev_io_uring_periodic_init;
      periodic_start = ev_io_uring_periodic_start;
      periodic_stop = ev_io_uring_periodic_stop;
      return EV_OK;
   }
   else if (config->ev_backend == EV_BACKEND_EPOLL)
   {
      loop_init = ev_epoll_init;
      loop_fork = ev_epoll_fork;
      loop_destroy = ev_epoll_destroy;
      loop_start = ev_epoll_loop;
      io_start = ev_epoll_io_start;
      io_stop = ev_epoll_io_stop;
      periodic_init = ev_epoll_periodic_init;
      periodic_start = ev_epoll_periodic_start;
      periodic_stop = ev_epoll_periodic_stop;
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
      return EV_OK;
   }
#endif /* HAVE_LINUX */
   return EV_ERROR;
}

struct event_loop*
pgagroal_event_loop_init(void)
{
   
   static bool context_is_set = false;

   loop = calloc(1, sizeof(struct event_loop));
   sigemptyset(&loop->sigset);

   if (!context_is_set)
   {

#if HAVE_LINUX
      /* io_uring context */
      entries = 32;
      params.cq_entries = 64;

      params.flags = 0;
      params.flags |= IORING_SETUP_CQSIZE; /* needed if I'm using cq_entries above */
      // params.flags |= IORING_SETUP_CLAMP; /* this likely reduces latency */
      params.flags |= IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_SINGLE_ISSUER; /* this likely reduces latency */

      /* params.flags |= IORING_SETUP_IOPOLL; */
      /* params.flags |= IORING_FEAT_SINGLE_MMAP; */

      /* NOTICE: SQPOLL might create a polling thread for each io_uring loop,
       * which might be overkill and hurt performance */
      sq_poll = false; /* puts too much pressure on the system or im using it wrong */
      use_huge = false; /* TODO: not implemented */
      fast_poll = false; /* TODO: haven't even been able to init the loop with this yet */
      mshot = false;

      if (use_huge)
      {
         pgagroal_log_fatal("use_huge not implemented");
         goto error;
      }
      if (sq_poll)
      {
         params.flags |= IORING_SETUP_SQPOLL;
      }
      if (fast_poll)
      {
         params.flags |= IORING_FEAT_FAST_POLL;
      }

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
         pgagroal_log_fatal("Failed to event backend operations");
         goto error;
      }

      if (loop_init(loop))
      {
         pgagroal_log_fatal("Failed to initiate loop");
         goto error;
      }

      context_is_set = true;
   }
   else if (loop_init(loop))
   {
      pgagroal_log_fatal("Failed to initiate loop");
      goto error;
   }

   return loop;

error:
   free(loop);
   return NULL;
}

int
pgagroal_event_loop_run(struct event_loop* loop)
{
   return loop_start(loop);
}

int
pgagroal_event_loop_fork(struct event_loop* loop)
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
pgagroal_event_loop_destroy(struct event_loop* loop)
{
   int ret;
   if (!loop)
   {
      return EV_OK;
   }
   ret = loop_destroy(loop);
   struct io_watcher* w;
   for_each(w, loop->ihead)
   {
      pgagroal_disconnect(w->fds.worker.snd_fd);
   }
   free(loop);
   return ret;
}

void
pgagroal_event_loop_break(struct event_loop* loop)
{
   loop->running = false;
}

bool
pgagroal_event_loop_is_running(struct event_loop* loop)
{
   return loop->running;
}

int
pgagroal_io_accept_init(struct io_watcher* io_w, int listen_fd, io_cb cb)
{
   io_w->type = EV_ACCEPT;
   io_w->fds.main.listen_fd = listen_fd;
   io_w->fds.main.client_fd = -1;
   io_w->cb = cb;
   io_w->handler = NULL;

   return EV_OK;
}

int
pgagroal_io_worker_init(struct io_watcher* io_w, int rcv_fd, int snd_fd, io_cb cb)
{
   io_w->type = EV_WORKER;
   io_w->fds.worker.rcv_fd = rcv_fd;
   io_w->fds.worker.snd_fd = snd_fd;
   io_w->cb = cb;
   io_w->handler = NULL;

   return EV_OK;
}

int
pgagroal_io_start(struct event_loop* loop, struct io_watcher* w)
{
   list_add(w, loop->ihead);
   return io_start(loop, w);
}

int
pgagroal_io_stop(struct event_loop* loop, struct io_watcher* io_w)
{
   int ret = EV_OK;
   struct io_watcher** w;
   if (!loop)
   {
      pgagroal_log_debug("Loop is NULL");
      return EV_ERROR;
   }
   if (!io_w)
   {
      pgagroal_log_fatal("Target is NULL");
      return EV_ERROR;
   }
   io_stop(loop, io_w);
   list_delete(w, &loop->ihead, io_w, ret);
   return ret;
}

int
pgagroal_signal_init(struct signal_watcher* sig_w, signal_cb cb, int signum)
{
   sig_w->type = EV_SIGNAL;
   sig_w->signum = signum;
   sig_w->cb = cb;
   sig_w->next = NULL;
   return EV_OK;
}

int
pgagroal_signal_start(struct event_loop* loop, struct signal_watcher* sig_w)
{
   struct sigaction act;
   sigemptyset(&act.sa_mask);
   act.sa_sigaction = &signal_handler;
   act.sa_flags = SA_SIGINFO | SA_RESTART;
   if (sigaction(sig_w->signum, &act, NULL) == -1)
   {
      pgagroal_log_fatal("sigaction failed for signum %d", sig_w->signum);
      return EV_ERROR;
   }
   signal_watchers[sig_w->signum] = sig_w;

   return EV_OK;
}

int __attribute__((unused))
pgagroal_signal_stop(struct event_loop* loop,
                        struct signal_watcher* target)
{
   int ret = EV_OK;
   sigset_t tmp;

#if DEBUG
   if (!target)
   {
      /* reaching here is a bug, do not recover */
      pgagroal_log_fatal("BUG: target is NULL");
      exit(1);
   }
#endif

   sigemptyset(&tmp);
   sigaddset(&tmp, target->signum);
#if !HAVE_LINUX
   /* TODO FreeBSD catches SIGINT as soon as it is removed from
    * sigset. This should be handled in a better way.
    * This is left here as a way to "fix" the issue.
    */
   if (target->signum != SIGINT)
   {
#endif
   if (sigprocmask(SIG_UNBLOCK, &tmp, NULL) == -1)
   {
      pgagroal_log_fatal("sigprocmask error: %s");
      return EV_FATAL;
   }
#if !HAVE_LINUX
}
#endif

   return ret;
}

int
pgagroal_periodic_init(struct periodic_watcher* w, periodic_cb cb, int msec)
{
   if (periodic_init(w, msec))
   {
      pgagroal_log_fatal("Failed to initiate timer event");
      exit(1);
   }
   w->type = EV_PERIODIC;
   w->cb = cb;
   w->next = NULL;
   return EV_OK;
}

int
pgagroal_periodic_start(struct event_loop* loop, struct periodic_watcher* w)
{
   periodic_start(loop, w);
   list_add(w, loop->phead);
   return EV_OK;
}

int __attribute__((unused))
pgagroal_periodic_stop(struct event_loop* loop, struct periodic_watcher* target)
{
   int ret;
   struct periodic_watcher** w;
   if (!target)
   {
      pgagroal_log_debug("Target is NULL");
      return EV_ERROR;
   }
   ret = periodic_stop(loop, target);
   list_delete(w, &loop->phead, target, ret);
   return ret;
}

#if HAVE_LINUX

int
pgagroal_send_message(struct io_watcher *w)
{
   struct io_uring_sqe* sqe = NULL;
   struct io_uring_cqe* cqe = NULL;
   struct message *msg = pgagroal_memory_message();

   sqe = io_uring_get_sqe(&loop->ring);
   io_uring_sqe_set_data(sqe, 0);
   io_uring_prep_send(sqe, w->fds.worker.snd_fd, msg->data, msg->length, MSG_WAITALL | MSG_NOSIGNAL);

   io_uring_submit(&loop->ring);
   
   io_uring_wait_cqe(&loop->ring, &cqe);
   if (cqe->res < msg->length)
   {
      return MESSAGE_STATUS_ERROR;
   }
   return MESSAGE_STATUS_OK;
}

static inline void __attribute__((unused))
ev_io_uring_rearm_receive(struct event_loop* loop, struct io_watcher* w)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring);
   io_uring_sqe_set_data(sqe, w);
   io_uring_prep_recv_multishot(sqe, w->fds.worker.rcv_fd, NULL, 0, 0);
}

// static inline int __attribute__((unused))
// __io_uring_replenish_buffers(struct event_loop* loop, struct io_buf_ring* br,
//                              int bid_start, int bid_end)
// {
//    int count;
//    if (bid_end >= bid_start)
//    {
//       count = (bid_end - bid_start);
//    }
//    else
//    {
//       count = (bid_end + buf_count - bid_start);
//    }
//    for (int i = bid_start; i != bid_end; i = (i + 1) & (buf_count - 1))
//    {
//       io_uring_buf_ring_add(br->br, (void*)br->br->bufs[i].addr, buf_size, i,
//                             br_mask, 0);
//    }
//    io_uring_buf_ring_advance(br->br, count);
//    return EV_OK;
// }

static int
ev_io_uring_init(struct event_loop* loop)
{
   int ret;
   ret = io_uring_queue_init_params(entries, &loop->ring, &params);
   if (ret)
   {
      pgagroal_log_fatal("io_uring_queue_init_params error: %s", strerror(-ret));
      return EV_ERROR;
   }
   if (ret)
   {
      return EV_ERROR;
   }
   ret = io_uring_ring_dontfork(&loop->ring);
   if (ret)
   {
      pgagroal_log_fatal("io_uring_ring_dontfork error: %s", strerror(-ret));
      return EV_ERROR;
   }
   return EV_OK;
}

static int
ev_io_uring_destroy(struct event_loop* loop)
{
   io_uring_queue_exit(&loop->ring);
   return EV_OK;
}

static int
ev_io_uring_io_start(struct event_loop* loop, struct io_watcher* w)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring);
   struct message* msg = pgagroal_memory_message();
   io_uring_sqe_set_data(sqe, w);
   switch (w->type)
   {
      case EV_ACCEPT:
         io_uring_prep_multishot_accept(sqe, w->fds.main.listen_fd, NULL, NULL, 0);
         break;
      case EV_WORKER:
         if (mshot) {
                io_uring_prep_recv_multishot(sqe, w->fds.worker.rcv_fd, msg->data, buf_size, 0);
         }
         else {
                io_uring_prep_recv(sqe, w->fds.worker.rcv_fd, msg->data, buf_size, 0);
         }
         break;
      default:
         pgagroal_log_fatal("unknown event type: %d", w->type);
         exit(1);
   }
   return EV_OK;
}

static int
ev_io_uring_io_stop(struct event_loop* loop, struct io_watcher* target)
{
   int ret = EV_OK;
   struct io_uring_sqe* sqe;
   /* NOTE: When io_stop is called it may never return to a loop
    * where sqes are submitted. Flush these sqes so the get call
    * doesn't return NULL. */
   do
   {
      sqe = io_uring_get_sqe(&loop->ring);
      if (sqe)
      {
         break;
      }
      io_uring_submit(&loop->ring);
   }
   while (1);
   io_uring_prep_cancel64(sqe, (uint64_t)target, 0); /* TODO: flags? */
   return ret;
}

static int
ev_io_uring_periodic_init(struct periodic_watcher* w, int msec)
{
   w->ts = (struct __kernel_timespec) {
        .tv_sec = msec / 1000,
        .tv_nsec = (msec % 1000) * 1000000
   };
   return EV_OK;
}

static int
ev_io_uring_periodic_start(struct event_loop* loop,
                           struct periodic_watcher* w)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring);
   io_uring_sqe_set_data(sqe, w);
   io_uring_prep_timeout(sqe, &w->ts, 0, IORING_TIMEOUT_MULTISHOT);
   return EV_OK;
}

static int
ev_io_uring_periodic_stop(struct event_loop* loop,
                          struct periodic_watcher* w)
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
ev_io_uring_loop(struct event_loop* loop)
{
   int ret = EV_ERROR;
   int events;
   int to_wait = 1; /* at first, wait for any 1 event */
   unsigned int head;
   struct io_uring_cqe* cqe = NULL;
   struct __kernel_timespec* ts = NULL;
   struct __kernel_timespec idle_ts = {
      .tv_sec = 0,
      .tv_nsec = 100000LL, /* seems best with 10000LL ms for most loads */
   };

   loop->running = true;
   while (loop->running)
   {
      ts = &idle_ts;
      io_uring_submit_and_wait_timeout(&loop->ring, &cqe, to_wait, ts, NULL);

      /* Good idea to leave here to see what happens */
      if (*loop->ring.cq.koverflow)
      {
         pgagroal_log_error("io_uring overflow %u", *loop->ring.cq.koverflow);
         return EV_FATAL;
      }
      if (*loop->ring.sq.kflags & IORING_SQ_CQ_OVERFLOW)
      {
         pgagroal_log_error("io_uring overflow");
         return EV_FATAL;
      }

      events = 0;
      io_uring_for_each_cqe(&(loop->ring), head, cqe){
         ret = ev_io_uring_handler(loop, cqe);
         events++;
      }

      if (events)
      {
         io_uring_cq_advance(&loop->ring, events);
      }
   }
   return ret;
}

static int
ev_io_uring_fork(struct event_loop* loop)
{
   return EV_OK;
}

static int
ev_io_uring_handler(struct event_loop* loop, struct io_uring_cqe* cqe)
{
   int ret = EV_OK;
   struct io_watcher* io = (struct io_watcher*)io_uring_cqe_get_data(cqe);

   void* buf;

   /*
    * Cancelled requests will trigger the handler, but have NULL data.
    */
   if (!io)
   {
      return EV_OK;
   }

   /* io handler */
   switch (io->type)
   {
      case EV_PERIODIC:
         return ev_io_uring_periodic_handler(loop, (struct periodic_watcher*)io);
      case EV_ACCEPT:
         return ev_io_uring_accept_handler(loop, io, cqe);
      case EV_SEND:
         return EV_FATAL; // __io_uring_send_handler(loop, (struct io_watcher*)io, cqe);
      case EV_WORKER:
retry:
         ret = ev_io_uring_receive_handler(loop, (struct io_watcher*)io, cqe, &buf, false);
         switch (ret)
         {
            case EV_CONNECTION_CLOSED: /* connection closed */
               break;
            case EV_ERROR:
               pgagroal_log_info("retrying...");
               goto retry;
               break;
            // case EV_REPLENISH_BUFFERS:
            //    if (ev_io_uring_setup_more_buffers(loop))
            //    {
            //       return EV_ERROR;
            //    }
            //    goto retry;
            //    break;
         }
         break;
      default:
         /* reaching here is a bug, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", (struct io_watcher*)io->type);
         exit(1);
   }
   return ret;
}

static int
ev_io_uring_periodic_handler(struct event_loop* loop,
                            struct periodic_watcher* w)
{
   w->cb(loop, w, 0);
   return EV_OK;
}

static int
ev_io_uring_accept_handler(struct event_loop* loop, struct io_watcher* w,
                          struct io_uring_cqe* cqe)
{
   w->fds.main.client_fd = cqe->res;
   w->cb(loop, w, EV_OK);
   return EV_OK;
}

static void
signal_handler(int signum, siginfo_t* si, void* p)
{
   struct signal_watcher* sig_w = signal_watchers[signum];
   sig_w->cb(loop, sig_w, EV_OK);
}

static int
ev_io_uring_receive_handler(struct event_loop* loop, struct io_watcher* w,
                           struct io_uring_cqe* cqe, void** _unused,
                           bool __unused)
{
   int ret;
   struct message* msg = pgagroal_memory_message();
   if (!(cqe->flags & IORING_CQE_F_BUFFER) && !(cqe->res)) {
      pgagroal_log_info("Connection closed");
      msg->data = NULL;
      msg->length = 0;
      ret = EV_CONNECTION_CLOSED;
   } else {
      msg->length = cqe->res;
      ret = EV_OK;
   }
   w->cb(loop, w, ret);

   /* restart worker watcher */
   ev_io_uring_io_start(loop, w);

   return ret;
}

// static int __attribute__((unused))
// __io_uring_receive_multishot_handler(struct event_loop* loop, struct io_watcher* w,
//                                      struct io_uring_cqe* cqe, void** unused,
//                                      bool is_proxy)
// {
//    struct io_buf_ring* br = &loop->br;
//    int bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
//    int total_in_bytes = cqe->res;
//    int cnt = 1;
// 
//    if (cqe->res == -ENOBUFS)
//    {
//       pgagroal_log_warn("ev: Not enough buffers");
//       return EV_REPLENISH_BUFFERS;
//    }
// 
//    if (!(cqe->flags & IORING_CQE_F_BUFFER) && !(cqe->res))
//    {
//       pgagroal_log_debug("ev: Connection closed");
//       return EV_CONNECTION_CLOSED;
//    }
//    else if (!(cqe->flags & IORING_CQE_F_MORE))
//    {
//       /* do not rearm receive. In fact, disarm anything so pgagroal can deal with
//        * read / write from sockets
//        */
//       w->data = NULL;
//       w->size = 0;
//       w->cb(loop, w, EV_ERROR);
//       return EV_CONNECTION_CLOSED;
//    }
// 
//    w->data = br->buf + (bid * buf_size);
//    w->size = total_in_bytes;
//    w->cb(loop, w, EV_OK);
//    io_uring_buf_ring_add(br->br, w->data, buf_size, bid, br_mask, bid);
//    io_uring_buf_ring_advance(br->br, cnt);
// 
//    return EV_OK;
// }

// static int
// ev_io_uring_setup_buffers(struct event_loop* loop)
// {
//    int ret;
//    int br_bgid = 0;
//    int br_flags = 0;
//    void* ptr;
// 
//    struct io_buf_ring* br = &loop->br;
//    if (use_huge)
//    {
//       pgagroal_log_fatal("io_uring use_huge not implemented");
//       return EV_ERROR;
//    }
//    if (posix_memalign(&br->buf, ALIGNMENT, buf_count * buf_size))
//    {
//       pgagroal_log_fatal("posix_memalign error: %s", strerror(errno));
//       return EV_ERROR;
//    }
// 
//    br->br = io_uring_setup_buf_ring(&loop->ring, buf_count, br_bgid, br_flags, &ret);
//    if (!br->br)
//    {
//       pgagroal_log_fatal("buffer ring register error %s", strerror(-ret));
//       return EV_ERROR;
//    }
// 
//    ptr = br->buf;
//    for (int i = 0; i < buf_count; i++)
//    {
//       io_uring_buf_ring_add(br->br, ptr, buf_size, i, br_mask, i);
//       ptr += buf_size;
//    }
//    io_uring_buf_ring_advance(br->br, buf_count);
// 
//    return EV_OK;
// }

// static int
// ev_io_uring_setup_more_buffers(struct event_loop* loop)
// {
//    int ret = EV_OK;
//    int br_bgid = 0;
//    int br_flags = 0;
//    void* ptr;
// 
//    struct io_buf_ring* br = &loop->br;
//    if (use_huge)
//    {
//       pgagroal_log_fatal("io_uring use_huge not implemented yet");
//       exit(1);
//    }
//    if (posix_memalign(&br->buf, ALIGNMENT, buf_count * buf_size))
//    {
//       pgagroal_log_fatal("posix_memalign");
//       return EV_FATAL;
//    }
// 
//    br->br =
//       io_uring_setup_buf_ring(&loop->ring, buf_count, br_bgid, br_flags, &ret);
//    if (!br->br)
//    {
//       pgagroal_log_fatal("buffer ring register failed %d", strerror(-ret));
//       return EV_FATAL;
//    }
// 
//    ptr = br->buf;
//    for (int i = 0; i < buf_count; i++)
//    {
//       io_uring_buf_ring_add(br->br, ptr, buf_size, i, br_mask, i);
//       ptr += buf_size;
//    }
//    io_uring_buf_ring_advance(br->br, buf_count);
// 
//    return EV_OK;
// }

void
_next_bid(struct event_loop* loop, int* bid)
{
   *bid = (*bid + 1) % buf_count;
}

int
ev_epoll_loop(struct event_loop* loop)
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
   int timeout = 10LL; /* ms */
#endif

   loop->running = true;
   while (loop->running)
   {
#if HAVE_EPOLL_PWAIT2
      nfds = epoll_pwait2(loop->epollfd, events, MAX_EVENTS, &timeout_ts,
                          &loop->sigset);
#else
      nfds = epoll_pwait(loop->epollfd, events, MAX_EVENTS, timeout, &loop->sigset);
#endif

      for (int i = 0; i < nfds; i++)
      {
        ret = ev_epoll_handler(loop, (void*)events[i].data.u64);
      }
   }
   return ret;
}

static int
ev_epoll_init(struct event_loop* loop)
{
   loop->epollfd = epoll_create1(epoll_flags);
   if (loop->epollfd == -1)
   {
      pgagroal_log_fatal("epoll_init error: %s", strerror(errno));
      return EV_FATAL;
   }
   return EV_OK;
}

static int
ev_epoll_fork(struct event_loop* loop)
{
   if (close(loop->epollfd) < 0)
   {
      pgagroal_log_error("close error: %s", strerror(errno));
      return EV_ERROR;
   }
   return EV_OK;
}

static int
ev_epoll_destroy(struct event_loop* loop)
{
   if (close(loop->epollfd) < 0)
   {
        pgagroal_log_error("close error: %s", strerror(errno));
        return EV_ERROR;
   }
   return EV_OK;
}

static int
ev_epoll_handler(struct event_loop* loop, void* w)
{
   if (((struct periodic_watcher*)w)->type == EV_PERIODIC)
   {
      return ev_epoll_periodic_handler(loop, (struct periodic_watcher*)w);
   }
   return ev_epoll_io_handler(loop, (struct io_watcher*)w);
}

static int
ev_epoll_periodic_init(struct periodic_watcher* w, int msec)
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

   /* no need to set it to non-blocking due to TFD_NONBLOCK */
   w->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK); 
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
ev_epoll_periodic_start(struct event_loop* loop, struct periodic_watcher* w)
{
   struct epoll_event event;
   event.events = EPOLLIN;
   event.data.u64 = (uint64_t)w;
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, w->fd, &event) == -1)
   {
      pgagroal_log_fatal("ev: epoll_ctl (%s)", strerror(errno));
      return EV_FATAL;
   }
   return EV_OK;
}

static int
ev_epoll_periodic_stop(struct event_loop* loop, struct periodic_watcher* w)
{
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, w->fd, NULL) == -1)
   {
      pgagroal_log_error("epoll_ctl error: %s", strerror(errno));
      return EV_ERROR;
   }
   return EV_OK;
}

static int
ev_epoll_periodic_handler(struct event_loop* loop,
                         struct periodic_watcher* w)
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
ev_epoll_io_start(struct event_loop* loop, struct io_watcher* io_w)
{
   struct epoll_event event;
   int fd;

   event.data.u64 = (uintptr_t)io_w;

   switch (io_w->type)
   {
      case EV_ACCEPT:
         fd = io_w->fds.main.listen_fd;
         event.events = EPOLLIN;
         break;
      case EV_WORKER:
         fd = io_w->fds.worker.rcv_fd;
         pgagroal_socket_nonblocking(io_w->fds.worker.snd_fd, true);
         /* TODO: Consider adding | EPOLLET; */
         event.events = EPOLLIN; 
         break;
      default:
         /* reaching here is a bug, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", io_w->type);
         exit(1);
   }

   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, fd, &event) == -1)
   {
      pgagroal_log_error("epoll_ctl error when adding fd %d : %s", fd, strerror(errno));
      return EV_FATAL;
   }

   return EV_OK;
}

static int
ev_epoll_io_stop(struct event_loop* ev, struct io_watcher* io_w)
{
   int fd;

   switch (io_w->type)
   {
      case EV_ACCEPT:
         fd = io_w->fds.main.listen_fd;
         break;
      case EV_WORKER:
         fd = io_w->fds.worker.rcv_fd;
         break;
      case EV_SEND:
         fd = io_w->fds.worker.snd_fd;
         break;
      default:
         /* reaching here is a bug, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", io_w->type);
         exit(1);
   }
   if (epoll_ctl(ev->epollfd, EPOLL_CTL_DEL, fd, NULL) == -1)
   {
      /* TODO: DOCUMENT: revisit this, what is this exactly? */
      if (errno == EBADF || errno == ENOENT || errno == EINVAL)
      {
         pgagroal_log_error("epoll_ctl error: %s", strerror(errno));
      }
      else
      {
         pgagroal_log_fatal("epoll_ctl error: %s", strerror(errno));
         return EV_FATAL;
      }
   }
   return EV_OK;
}

static int
ev_epoll_io_handler(struct event_loop* loop, struct io_watcher* io_w)
{
   switch (io_w->type)
   {
      case EV_ACCEPT:
         return ev_epoll_accept_handler(loop, io_w);
      case EV_SEND:
         return ev_epoll_send_handler(loop, io_w);
      case EV_WORKER:
         return ev_epoll_receive_handler(loop, io_w);
      default:
         pgagroal_log_fatal("unknown event type: %d", io_w->type);
         exit(1);
   }
}

static int
ev_epoll_accept_handler(struct event_loop* loop, struct io_watcher* w)
{
   int ret = EV_OK;
   int client_fd = accept(w->fds.main.listen_fd, NULL, NULL);
   if (client_fd == -1)
   {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
         pgagroal_log_error("accept error: %s", strerror(errno));
         ret = EV_ERROR;
      }
   }
   else
   {
      pgagroal_socket_nonblocking(client_fd, true);
      w->fds.main.client_fd = client_fd;
      w->cb(loop, w, ret);
   }

   return ret;
}

static int
ev_epoll_receive_handler(struct event_loop* loop, struct io_watcher* w)
{
   int ret = EV_OK;
   w->cb(loop, w, ret);
   return ret;
}

static int
ev_epoll_send_handler(struct event_loop* loop, struct io_watcher* w)
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
   timeout.tv_nsec = 10000000; /* 10 ms */

   set_running(ev);
   do
   {
      nfds = kevent(ev->kqueuefd, NULL, 0, events, MAX_EVENTS, &timeout);
      if (nfds == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }
         pgagroal_log_error("kevent");
         ret = EV_ERROR;
         loop_break(ev);
         break;
      }
      for (int i = 0; i < nfds; i++)
      {
         ret = __kqueue_handler(ev, &events[i]);
      }
   }
   while (is_running(ev));
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
__kqueue_destroy(struct ev_loop* loop)
{
   close(loop->kqueuefd);
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

int __attribute__((unused))
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
__kqueue_periodic_init(struct struct periodic_watcher* w, int msec)
{
   w->interval = msec;
   return EV_OK;
}

static int
__kqueue_periodic_start(struct ev_loop* ev, struct struct periodic_watcher* w)
{
   struct kevent kev;
   EV_SET(&kev, (uintptr_t)w, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_USECONDS,
          w->interval * 1000, w);
   if (kevent(ev->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent: timer add");
      return EV_ERROR;
   }
   return EV_OK;
}

static int
__kqueue_periodic_stop(struct ev_loop* ev, struct struct periodic_watcher* w)
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
   struct struct periodic_watcher* w = (struct struct periodic_watcher*)kev->udata;
   pgagroal_log_debug("%s");
   w->cb(ev, w, 0);
   return EV_OK;
}

static int
__kqueue_io_start(struct ev_loop* ev, struct struct io_watcher* w)
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
__kqueue_io_stop(struct ev_loop* ev, struct struct io_watcher* w)
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
   struct struct io_watcher* w = (struct struct io_watcher*)kev->udata;
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
__kqueue_receive_handler(struct ev_loop* loop, struct struct io_watcher* w)
{
   int ret = EV_OK;
   w->cb(loop, w, ret);
   return ret;
}

static int
__kqueue_send_handler(struct ev_loop* loop, struct struct io_watcher* w)
{
   int ret = EV_OK;
   w->cb(loop, w, ret);
   return ret;
}

static int
__kqueue_accept_handler(struct ev_loop* ev, struct struct io_watcher* w)
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

#endif /* HAVE_LINUX */
