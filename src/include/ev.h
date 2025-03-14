/*
 * Copyright (C) 2024 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_LINUX
#include <liburing.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#endif /* HAVE_LINUX */

#define ALIGNMENT sysconf(_SC_PAGESIZE)

#define BUFFER_COUNT 2
#define MAX_EVENTS 128

/**
 * Constants used to define the supported
 * event backends.
 */

typedef enum ev_backend {
  EV_BACKEND_INVALID = -2,
  EV_BACKEND_EMPTY = -1,
  EV_BACKEND_AUTO = 0,
  EV_BACKEND_IO_URING,
  EV_BACKEND_EPOLL,
  EV_BACKEND_KQUEUE,
} ev_backend_t;

#if HAVE_LINUX
#define DEFAULT_EV_BACKEND EV_BACKEND_IO_URING
#else
#define DEFAULT_EV_BACKEND EV_BACKEND_KQUEUE
#endif

enum ev_type {
  EV_INVALID = 0,
  EV_ACCEPT = 1,
  EV_WORKER = 2,
  EV_SEND,
  EV_SIGNAL,
  EV_PERIODIC,
};

enum ev_return_codes {
  EV_OK = 0,
  EV_ERROR,
  EV_FATAL,
  EV_CONNECTION_CLOSED,
  EV_REPLENISH_BUFFERS,
  EV_REARMED,
  EV_ALLOC_ERROR,
};

#define for_each(w, head) for (w = head.next; w; w = w->next)

#define list_add(w, head)                                                      \
        do {                                                                   \
           w->next = head.next;                                                \
           head.next = w;                                                      \
        } while (0)

#define list_delete(w, head, target, ret)                                      \
        do {                                                                   \
           for (w = head.next; *w && *w != target; w = &(*w)->next)            \
           ;                                                                   \
              if (!target->next) {                                             \
                 *w = NULL;                                                    \
              } else {                                                         \
                 *w = target->next;                                            \
              }                                                                \
        } while (0)

/**
 * @union sockaddr_u
 * @brief Socket address union for IPv4 and IPv6
 *
 * Stores either an IPv4 or IPv6 socket address
 */
union sockaddr_u {
  struct sockaddr_in addr4;  /**< IPv4 socket address structure. */
  struct sockaddr_in6 addr6; /**< IPv6 socket address structure. */
};

struct event_loop;

#if HAVE_LINUX
/**
 * @struct io_buf_ring
 * @brief Represents a buffer ring for I/O operations with io_uring.
 *
 * The io_buf_ring structure holds pointers to an io_uring buffer ring and
 * a generic buffer, along with a buffer group ID (bgid).
 */
struct io_buf_ring {
  struct io_uring_buf_ring *br;   /**< Pointer to the io_uring buffer ring internal structure. */
  void *buf; /**< Pointer to the buffer used for I/O operations. */
  int sz;                                                             /**< Size of the data buffer. */
};
#endif /* HAVE_LINUX */



/**
 * @struct io_watcher
 * @brief I/O watcher for the event loop
 *
 * Monitors file descriptors for I/O readiness events (e.g., read or write)
 */
struct io_watcher {
  enum ev_type type;                                                    /**< Event type. */
  union {
    struct {
        int client_fd;                                                  /**< TODO. */
        int listen_fd;                                                  /**< TODO. */
    } main;
    struct {
      int rcv_fd;                                                       /**< TODO. */
      int snd_fd;                                                       /**< TODO. */
    } worker;
    int __fds[2];
  } fds;                                                                /**< TODO. */
  // struct io_buf_ring br;
  bool ssl;                                                             /**< Indicates if SSL/TLS is used on this connection. */
  struct io_watcher *next;                                              /**< Pointer to the next watcher in the linked list. */
  int bgid;                                                             /**< TODO: will be used */
  void (*cb)(struct event_loop *, struct io_watcher *watcher, int err); /**< Event callback. */
  void (*handler)(struct io_watcher *watcher);                          /**< TODO: might be used */
};

/**
 * @struct signal_watcher
 * @brief Signal watcher for the event loop
 *
 * Monitors and handles specific signals received by the process
 */
struct signal_watcher {
  enum ev_type type;                                                        /**< Event type. */
  int signum;                                                               /**< Signal number to watch for. */
  struct signal_watcher *next;                                              /**< Pointer to the next signal watcher. */
  void (*cb)(struct event_loop *, struct signal_watcher *watcher, int err); /**< Event callback. */
  void (*handler)(struct event_loop *, struct io_watcher *watcher);         /**< TODO: might be used */
};

/**
 * @struct periodic_watcher
 * @brief Periodic timer watcher for the event loop
 *
 * Triggers callbacks at regular intervals specified in milliseconds
 */
struct periodic_watcher {
  enum ev_type type; /**< Event type. */
#if HAVE_LINUX
  struct __kernel_timespec ts; /**< Timespec struct for io_uring loop. */
  int fd; /**< File descriptor for epoll-based periodic watcher. */
#else
  int interval; /**< Interval for kqueue timer. */
#endif                           /* HAVE_LINUX */
  struct periodic_watcher *next; /**< Pointer to the next periodic watcher. */
  void (*cb)(struct event_loop *, struct periodic_watcher *watcher,
             int err); /**< Event callback. */
  void (*handler)(struct event_loop *,
                  struct io_watcher *watcher); /**< TODO: might be used */
};

/**
 * @union watcher
 * @brief General watcher union for the event loop
 */
union watcher {
  struct io_watcher *io;             /**< Pointer to an I/O watcher. */
  struct signal_watcher *signal;     /**< Pointer to a signal watcher. */
  struct periodic_watcher *periodic; /**< Pointer to a periodic watcher. */
};

/**
 * @struct ev_ops
 * @brief Event loop backend operations
 *
 * Contains function pointers for initializing and controlling the event loop,
 * allowing for different backend implementations.
 */
struct ev_ops {
  int (*init)(struct event_loop *loop); /**< Initializes the event loop backend. */
  int (*loop)(struct event_loop *loop); /**< Runs the event loop, processing events. */
  int (*io_start)(struct event_loop *loop, struct io_watcher *watcher); /**< Starts an I/O watcher in the event loop. */
  int (*io_stop)(struct event_loop *loop, struct io_watcher *watcher); /**< Stops an I/O watcher in the event loop. */
  int (*signal_init)(struct event_loop *loop, struct signal_watcher *watcher); /**< Initializes a signal watcher. */
  int (*signal_start)(struct event_loop *loop, struct signal_watcher *watcher); /**< Starts a signal watcher in the event loop. */
  int (*signal_stop)(struct event_loop *loop, struct signal_watcher *watcher); /**< Stops a signal watcher in the event loop. */
  int (*periodic_init)(struct event_loop *loop, struct periodic_watcher *watcher); /**< Initializes a periodic watcher. */
  int (*periodic_start)(struct event_loop *loop, struct periodic_watcher *watcher); /**< Starts a periodic watcher in the event loop. */
  int (*periodic_stop)(struct event_loop *loop, struct periodic_watcher *watcher); /**< Stops a periodic watcher in the event loop. */
};

/**
 * @struct event_loop
 * @brief Main event loop structure.
 *
 * Handles the execution and coordination of events using the specified
 * backend.
 */
struct event_loop {
  volatile bool running;         /**< Flag indicating if the event loop is running. */
  atomic_bool atomic_running;    /**< Atomic flag for thread-safe running state. */
  struct io_watcher ihead;       /**< Head of the I/O watchers linked list. */
  struct signal_watcher shead;   /**< Head of the signal watchers linked list. */
  struct periodic_watcher phead; /**< Head of the signal watchers linked list. */
  sigset_t sigset;               /**< Signal set used for handling signals in the event loop. */
  struct ev_ops ops;             /**< Backend operations for the event loop. */
#if HAVE_LINUX
  struct io_uring_cqe *cqe;
  struct io_uring ring;
  int bid;                       /**< io_uring: Next buffer id. */
  /**
   * TODO: Implement iovecs.
   *   int iovecs_nr;
   *   struct iovec *iovecs;
   */
  int epollfd; /**< File descriptor for the epoll instance (used with epoll
                  backend). */
#else
  int kqueuefd; /**< File descriptor for the kqueue instance (used with kqueue
                   backend). */
#endif          /* HAVE_LINUX */
  void *buffer; /**< Pointer to a buffer used to read in bytes. */
};

typedef void (*io_cb)(struct event_loop *, struct io_watcher *watcher, int err);
typedef void (*signal_cb)(struct event_loop *, struct signal_watcher *watcher, int err);
typedef void (*periodic_cb)(struct event_loop *, struct periodic_watcher *watcher, int err);

/**
 * Initialize a new event loop
 * @param config Pointer to the configuration struct
 * @return Pointer to the initialized event loop
 */
struct event_loop *pgagroal_event_loop_init(void);

/**
 * Start the main event loop
 * @param loop Pointer to the event loop struct
 * @return Return code
 */
int pgagroal_event_loop_run(struct event_loop *loop);

/**
 * Break the event loop, stopping its execution
 * @param loop Pointer to the event loop struct
 */
void pgagroal_event_loop_break(struct event_loop *loop);

/**
 * Destroy the event loop, freeing only the strictly necessary resources that
 * need to be freed.
 *
 * @param loop Pointer to the event loop struct
 * @return Return code
 */
int pgagroal_event_loop_destroy(struct event_loop *loop);

/**
 * Closes the file descriptors used by the loop of the parent process.
 *
 * @param loop Pointer to the loop that should be freed by the child process
 * @return Return code
 */
int pgagroal_event_loop_fork(struct event_loop *loop);

/**
 * Check if the event loop is currently running
 * @param loop Pointer to the event loop struct
 * @return True if the loop is running, false otherwise
 */
bool pgagroal_event_loop_is_running(struct event_loop *loop);

/**
 * Initialize the watcher for accept event
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @return Return code
 */
int pgagroal_io_accept_init(struct io_watcher *w, int accept_fd, io_cb cb);

/**
 * Initialize the watcher for receive events
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @return Return code
 */
int pgagroal_io_worker_init(struct io_watcher *w, int snd_fd, int rcv_fd, io_cb cb);

/**
 * Initialize the watcher for sending IO operations
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @param buf Pointer to the buffer to be sent
 * @param buf_len Length of the buffer to be sent
 * @return Return code
 * TODO remove
 */
// int pgagroal_io_send_init(struct io_watcher *w, int fd, io_cb cb, void *buf, int buf_len);

/**
 * Start the watcher for an IO event in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the io event watcher struct
 * @return Return code
 */
int pgagroal_io_start(struct event_loop *loop, struct io_watcher *w);

/**
 * Stop the watcher for an IO event in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the io event watcher struct
 * @return Return code
 */
int pgagroal_io_stop(struct event_loop *loop, struct io_watcher *w);

/**
 * Initialize the watcher for periodic timeout events
 * @param w Pointer to the periodic event watcher struct
 * @param cb Callback executed on timeout
 * @param msec Interval in milliseconds for the periodic event
 * @return Return code
 */
int pgagroal_periodic_init(struct periodic_watcher *w, periodic_cb cb, int msec);

/**
 * Start the watcher for a periodic timeout in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the periodic event watcher struct
 * @return Return code
 */
int pgagroal_periodic_start(struct event_loop *loop, struct periodic_watcher *w);

/**
 * Stop the watcher for a periodic timeout in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the periodic event watcher struct
 * @return Return code
 */
int pgagroal_periodic_stop(struct event_loop *loop, struct periodic_watcher *w);

/**
 * Initialize the watcher for signal events
 * @param w Pointer to the signal event watcher struct
 * @param cb Callback executed when signal is received
 * @param signum Signal number to watch
 * @return Return code
 */
int pgagroal_signal_init(struct signal_watcher *w, signal_cb cb, int signum);

/**
 * Start the watcher for a signal in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the signal event watcher struct
 * @return Return code
 */
int pgagroal_signal_start(struct event_loop *loop, struct signal_watcher *w);

/**
 * Stop the watcher for a signal in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the signal event watcher struct
 * @return Return code
 */
int pgagroal_signal_stop(struct event_loop *loop, struct signal_watcher *w);

/**
 * TODO
 * @param
 * @param
 * @param
 * @return
 */
int pgagroal_io_check_send(int size);

/**
 * TODO
 * @param
 * @param
 * @param
 * @return
 */
int pgagroal_send_message(struct io_watcher *w);

#endif /* EV_H */
