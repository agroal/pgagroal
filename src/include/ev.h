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

#ifndef EV_H
#define EV_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgagroal */
#include <pgagroal.h>

/* system */
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <unistd.h>

#if HAVE_LINUX
#include <sys/signalfd.h>
#endif

#if HAVE_URING
#include <liburing.h>
#endif

#if HAVE_EPOLL
#include <sys/epoll.h>
#endif

#define ALIGNMENT sysconf(_SC_PAGESIZE)  /* TODO: will be used for huge pages support */
#define BUFFER_SIZE 65535

#define BUFFER_COUNT 8

#define INITIAL_BUFFER_SIZE 65535
#ifndef MAX_BUFFER_SIZE
#define MAX_BUFFER_SIZE 65535
#endif

#define MAX_EVENTS 128

#define FALLBACK_BACKEND "epoll"

/* TODO(hc): cleanup unused enums */
enum ev_type {
   EV_ACCEPT    = 0,
   EV_RECEIVE   = 1,
   EV_SEND      = 2,
   CONNECT      = 3,
   SOCKET       = 4,
   READ         = 5,
   WRITE        = 6,
   EV_SIGNAL    = 8,
   EV_PERIODIC  = 9,
};

/*
 * TODO: Improve error handling for pgagroal_ev
 */
enum ev_return_codes {
   EV_OK = 0,
   EV_ERROR = 1,
   EV_CLOSE_FD,
   EV_REPLENISH_BUFFERS,
   EV_REARMED,
   EV_ALLOC_ERROR,
};

enum ev_backend {
   EV_BACKEND_IO_URING = (1 << 1),
   EV_BACKEND_EPOLL    = (1 << 2),
   EV_BACKEND_KQUEUE   = (1 << 3),
};

/**
 * @union sockaddr_u
 * @brief Socket address union for IPv4 and IPv6.
 *
 * Stores either an IPv4 or IPv6 socket address.
 */
union sockaddr_u
{
   struct sockaddr_in addr4; /**< IPv4 socket address structure. */
   struct sockaddr_in6 addr6; /**< IPv6 socket address structure. */
};

/**
 * @struct ev_context
 * @brief Context for the event handling subsystem.
 *
 * This structure is used to configure and manage state for event handling, the
 * same struct is valid for any backend. If the backend does not use one flag,
 * the library will just ignore it.
 */
struct ev_context
{
   /* filled in by the client */
   int epoll_flags;                     /**< Flags for epoll instance creation. */

   int entries;                         /**< io_uring flag */
   bool napi;                           /**< io_uring flag */
   bool sqpoll;                         /**< io_uring flag */
   bool use_huge;                       /**< io_uring flag */
   bool defer_tw;                       /**< io_uring flag */
   bool snd_ring;                       /**< io_uring flag */
   bool snd_bundle;                     /**< io_uring flag */
   bool fixed_files;                    /**< io_uring flag */

   bool ipv6;                           /**< Indicates if IPv6 is used. */
   bool multithreading;                 /**< Enable multithreading for a loop. */

   bool no_use_buffers;                 /**< Disable use of ring-mapped buffers. */
   int buf_size;                        /**< Size of the ring-mapped buffers. */
   int buf_count;                       /**< Number of ring-mapped buffers. */

   enum ev_backend backend;             /**< Event loop backend in use. */

   /* filled in by the library */
   int br_mask;                         /**< Buffer ring mask value. */

#if HAVE_URING
   struct io_uring_params params;
#endif
};

struct ev_loop;

/**
 * @struct ev_io
 * @brief I/O watcher for the event loop.
 *
 * Monitors file descriptors for I/O readiness events (e.g., read or write).
 */
typedef struct ev_io
{
   enum ev_type type;                                           /**< Event type. */
   int slot;                                                    /**< *CURRENTLY UNUSED* Slot number associated with the poll. */
   int fd;                                                      /**< File descriptor to watch. */
   int client_fd;                                               /**< Client's file descriptor, if applicable. */
   void* data;                                                  /**< Pointer to received data. */
   int size;                                                    /**< Size of the data buffer. */
   bool ssl;                                                    /**< Indicates if SSL/TLS is used on this connection. */
   struct ev_io* next;                                          /**< Pointer to the next watcher in the linked list. */
   void (*cb)(struct ev_loop*, struct ev_io* watcher, int err); /**< Event callback. */
} ev_io;

/**
 * @struct ev_signal
 * @brief Signal watcher for the event loop.
 *
 * Monitors and handles specific signals received by the process.
 */
typedef struct ev_signal
{
   enum ev_type type;                                               /**< Event type. */
   int slot;                                                        /**< *CURRENTLY UNUSED* Slot number associated with the poll. */
   int signum;                                                      /**< Signal number to watch for. */
   struct ev_signal* next;                                          /**< Pointer to the next signal watcher. */
   void (*cb)(struct ev_loop*, struct ev_signal* watcher, int err); /**< Event callback. */
} ev_signal;

/**
 * @struct ev_periodic
 * @brief Periodic timer watcher for the event loop.
 *
 * Triggers callbacks at regular intervals specified in milliseconds.
 */
typedef struct ev_periodic
{
   enum ev_type type;                                                  /**< Event type. */
   int slot;                                                           /**< *CURRENTLY UNUSED* Slot number associated with the poll. */
#if HAVE_URING
   struct __kernel_timespec ts;                                        /**< Timespec struct for io_uring loop. */
#endif
#if HAVE_EPOLL
   int fd;                                                             /**< File descriptor for epoll-based periodic watcher. */
#endif
#if HAVE_KQUEUE
   int interval;                                                       /**< Interval for kqueue timer. */
#endif
   struct ev_periodic* next;                                           /**< Pointer to the next periodic watcher. */
   void (*cb)(struct ev_loop*, struct ev_periodic* watcher, int err);  /**< Event callback. */
} ev_periodic;

/**
 * @union ev_watcher
 * @brief General watcher union for the event loop.
 */
typedef union ev_watcher
{
   struct ev_io* io;                    /**< Pointer to an I/O watcher. */
   struct ev_signal* signal;            /**< Pointer to a signal watcher. */
   struct ev_periodic* periodic;        /**< Pointer to a periodic watcher. */
} ev_watcher;

#if HAVE_URING

/**
 * @struct io_buf_ring
 * @brief Represents a buffer ring for I/O operations with io_uring.
 *
 * The io_buf_ring structure holds pointers to an io_uring buffer ring and
 * a generic buffer, along with a buffer group ID (bgid).
 */
struct io_buf_ring
{
   struct io_uring_buf_ring* br;  /**< Pointer to the io_uring buffer ring internal structure. */
   void* buf;                     /**< Pointer to the buffer used for I/O operations. */
   int bgid;                      /**< **CURRENTLY UNUSED** Buffer group ID for identifying buffer groups in the ring. */
};

#endif

/**
 * @struct ev_ops
 * @brief Event loop backend operations.
 *
 * Contains function pointers for initializing and controlling the event loop,
 * allowing for different backend implementations.
 */
struct ev_ops
{
   int (*init)(struct ev_loop* loop); /**< Initializes the event loop backend. */
   int (*loop)(struct ev_loop* loop); /**< Runs the event loop, processing events. */
   int (*io_start)(struct ev_loop* loop, struct ev_io* watcher); /**< Starts an I/O watcher in the event loop. */
   int (*io_stop)(struct ev_loop* loop, struct ev_io* watcher); /**< Stops an I/O watcher in the event loop. */
   int (*signal_init)(struct ev_loop* loop, struct ev_signal* watcher); /**< Initializes a signal watcher. */
   int (*signal_start)(struct ev_loop* loop, struct ev_signal* watcher); /**< Starts a signal watcher in the event loop. */
   int (*signal_stop)(struct ev_loop* loop, struct ev_signal* watcher); /**< Stops a signal watcher in the event loop. */
   int (*periodic_init)(struct ev_loop* loop, struct ev_periodic* watcher); /**< Initializes a periodic watcher. */
   int (*periodic_start)(struct ev_loop* loop, struct ev_periodic* watcher); /**< Starts a periodic watcher in the event loop. */
   int (*periodic_stop)(struct ev_loop* loop, struct ev_periodic* watcher); /**< Stops a periodic watcher in the event loop. */
};

/**
 * @struct ev_loop
 * @brief Main event loop structure.
 *
 * Manages the event loop, including I/O, signal, and periodic watchers.
 * It handles the execution and coordination of events using the specified backend.
 *
 * TODO maybe `struct ev_loop` could be separated into ev_loop{,_uring,_epoll,_kqueue} so that it
 * could be dinamically plugged into ev_loop.
 */
struct ev_loop
{
   volatile bool running;               /**< Flag indicating if the event loop is running. */
   atomic_bool atomic_running;          /**< Atomic flag for thread-safe running state. */
   struct ev_context ctx;               /**< Context containing configuration and state. */

   struct ev_io ihead;                  /**< Head of the I/O watchers linked list. */
   struct ev_signal shead;              /**< Head of the signal watchers linked list. */
   struct ev_periodic phead;            /**< Head of the signal watchers linked list. */

   sigset_t sigset;                     /**< Signal set used for handling signals in the event loop. */

   struct ev_ops ops;                   /**< Backend operations for the event loop. */

   struct configuration* config;        /**< Pointer to pgagroal global configuration. */

#if HAVE_URING
   struct io_uring_cqe* cqe;

   struct io_uring ring;
   struct io_buf_ring in_br;
   struct io_buf_ring out_br;
   /* TODO: Consider removing the usage of .bid */
   int bid;                             /**< io_uring: Next buffer id. */

   /* TODO: Implement iovecs.
    *   int iovecs_nr;
    *   struct iovec *iovecs;
    */

#endif /* HAVE_URING */

#if HAVE_EPOLL
   int epollfd;                         /**< File descriptor for the epoll instance (used with epoll backend). */
#endif

#if HAVE_KQUEUE
   int kqueuefd;                        /**< File descriptor for the kqueue instance (used with kqueue backend). */
#endif

   void* buffer;                        /**< Pointer to a buffer used to read in bytes. */

};

typedef void (*io_cb)(struct ev_loop*, struct ev_io* watcher, int err);
typedef void (*signal_cb)(struct ev_loop*, struct ev_signal* watcher, int err);
typedef void (*periodic_cb)(struct ev_loop*, struct ev_periodic* watcher, int err);

/** This set of functions initializes, starts, breaks, and destroys an event loop.
 * @param w:
 * @param fd:
 * @param loop:
 * @param addr:
 * @param buf:
 * @param buf_len:
 * @param cb:
 * @param bid:
 * @return
 */
struct ev_loop* pgagroal_ev_init(struct configuration* config);
int pgagroal_ev_loop_destroy(struct ev_loop* loop);
int pgagroal_ev_loop(struct ev_loop* loop);
void pgagroal_ev_loop_break(struct ev_loop* loop);

/** This function should be called after each fork to destroy a copied loop.
 * @param loop: loop that should have information freed by the child process.
 */
int pgagroal_ev_loop_fork(struct ev_loop** loop);
static inline bool
pgagroal_ev_loop_is_running(struct ev_loop* ev)
{
   return ev->running;
}
static inline bool
pgagroal_ev_atomic_loop_is_running(struct ev_loop* ev)
{
   return atomic_load(&ev->atomic_running);
}

/** This set of functions initialize, start and stop watchers for io operations.
 * @param w:
 * @param fd:
 * @param ev_loop:
 * @param addr:
 * @param buf:
 * @param buf_len:
 * @param cb:
 * @param bid:
 * @return
 */
int _ev_io_init(struct ev_io* w, int, int, io_cb, void*, int, int);
int pgagroal_ev_io_accept_init(struct ev_io* w, int fd, io_cb cb);
int pgagroal_ev_io_read_init(struct ev_io* w, int fd, io_cb cb);
int pgagroal_ev_io_receive_init(struct ev_io* w, int fd, io_cb cb);
int pgagroal_ev_io_connect_init(struct ev_io* w, int fd, io_cb cb, union sockaddr_u* addr);
int pgagroal_ev_io_send_init(struct ev_io* w, int fd, io_cb cb, void* buf, int buf_len, int bid);
int pgagroal_ev_io_start(struct ev_loop* loop, struct ev_io* w);
int pgagroal_ev_io_stop(struct ev_loop* loop, struct ev_io* w);

/** This set of functions initialize, start and stop watchers for periodic timeouts.
 * @param w:
 * @param ev_loop:
 * @param msec:
 * @param cb:
 * @return
 */
int pgagroal_ev_periodic_init(struct ev_periodic* w, periodic_cb cb, int msec);
int pgagroal_ev_periodic_start(struct ev_loop* loop, struct ev_periodic* w);
int pgagroal_ev_periodic_stop(struct ev_loop* loop, struct ev_periodic* w);

/** This set of functions initialize, start and stop watchers for io operations.
 * @param w:
 * @param ev_loop:
 * @param signum:
 * @param cb:
 * @return
 *
 */
int pgagroal_ev_signal_init(struct ev_signal* w, signal_cb cb, int signum);
int pgagroal_ev_signal_start(struct ev_loop* loop, struct ev_signal* w);
int pgagroal_ev_signal_stop(struct ev_loop* loop, struct ev_signal* w);

#ifdef __cplusplus
}
#endif

#endif /* EV_H */
