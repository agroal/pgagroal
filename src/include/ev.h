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
#include <liburing.h>
#include <sys/epoll.h>
#endif /* HAVE_LINUX */

#define ALIGNMENT sysconf(_SC_PAGESIZE)

#define BUFFER_COUNT 2
#define MAX_EVENTS 128

/**
 * Constants used to define the supported
 * event backends.
 */

typedef enum ev_backend
{
   EV_BACKEND_AUTO     =  0,
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
   EV_ACCEPT,
   EV_RECEIVE,
   EV_SEND,
   EV_SIGNAL,
   EV_PERIODIC,
};

enum ev_return_codes {
   EV_OK = 0,
   EV_ERROR,
   EV_CONNECTION_CLOSED,
   EV_REPLENISH_BUFFERS,
   EV_REARMED,
   EV_ALLOC_ERROR,
};

/**
 * @union sockaddr_u
 * @brief Socket address union for IPv4 and IPv6
 *
 * Stores either an IPv4 or IPv6 socket address
 */
union sockaddr_u
{
   struct sockaddr_in addr4; /**< IPv4 socket address structure. */
   struct sockaddr_in6 addr6; /**< IPv6 socket address structure. */
};

struct ev_loop;

/**
 * @struct ev_io
 * @brief I/O watcher for the event loop
 *
 * Monitors file descriptors for I/O readiness events (e.g., read or write)
 */
typedef struct ev_io
{
   enum ev_type type;                                           /**< Event type. */
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
 * @brief Signal watcher for the event loop
 *
 * Monitors and handles specific signals received by the process
 */
typedef struct ev_signal
{
   enum ev_type type;                                               /**< Event type. */
   int signum;                                                      /**< Signal number to watch for. */
   struct ev_signal* next;                                          /**< Pointer to the next signal watcher. */
   void (*cb)(struct ev_loop*, struct ev_signal* watcher, int err); /**< Event callback. */
} ev_signal;

/**
 * @struct ev_periodic
 * @brief Periodic timer watcher for the event loop
 *
 * Triggers callbacks at regular intervals specified in milliseconds
 */
typedef struct ev_periodic
{
   enum ev_type type;                                                  /**< Event type. */
#if HAVE_LINUX
   struct __kernel_timespec ts;                                        /**< Timespec struct for io_uring loop. */
   int fd;                                                             /**< File descriptor for epoll-based periodic watcher. */
#else
   int interval;                                                       /**< Interval for kqueue timer. */
#endif /* HAVE_LINUX */
   struct ev_periodic* next;                                           /**< Pointer to the next periodic watcher. */
   void (*cb)(struct ev_loop*, struct ev_periodic* watcher, int err);  /**< Event callback. */
} ev_periodic;

/**
 * @union ev_watcher
 * @brief General watcher union for the event loop
 */
typedef union ev_watcher
{
   struct ev_io* io;                    /**< Pointer to an I/O watcher. */
   struct ev_signal* signal;            /**< Pointer to a signal watcher. */
   struct ev_periodic* periodic;        /**< Pointer to a periodic watcher. */
} ev_watcher;

#if HAVE_LINUX
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
};
#endif /* HAVE_LINUX */

/**
 * @struct ev_ops
 * @brief Event loop backend operations
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
 */
struct ev_loop
{
   volatile bool running;               /**< Flag indicating if the event loop is running. */
   atomic_bool atomic_running;          /**< Atomic flag for thread-safe running state. */
   struct ev_io ihead;                  /**< Head of the I/O watchers linked list. */
   struct ev_signal shead;              /**< Head of the signal watchers linked list. */
   struct ev_periodic phead;            /**< Head of the signal watchers linked list. */
   sigset_t sigset;                     /**< Signal set used for handling signals in the event loop. */
   struct ev_ops ops;                   /**< Backend operations for the event loop. */
#if HAVE_LINUX
   struct io_uring_cqe* cqe;
   struct io_uring ring;
   struct io_buf_ring br;
   int bid;                             /**< io_uring: Next buffer id. */
   /**
    * TODO: Implement iovecs.
    *   int iovecs_nr;
    *   struct iovec *iovecs;
    */
   int epollfd;                         /**< File descriptor for the epoll instance (used with epoll backend). */
#else
   int kqueuefd;                        /**< File descriptor for the kqueue instance (used with kqueue backend). */
#endif /* HAVE_LINUX */
   void* buffer;                        /**< Pointer to a buffer used to read in bytes. */

};

typedef void (*io_cb)(struct ev_loop*, struct ev_io* watcher, int err);
typedef void (*signal_cb)(struct ev_loop*, struct ev_signal* watcher, int err);
typedef void (*periodic_cb)(struct ev_loop*, struct ev_periodic* watcher, int err);

/**
 * Initialize a new event loop
 * @param config Pointer to the configuration struct
 * @return Pointer to the initialized event loop
 */
struct ev_loop*
pgagroal_ev_init(void);

/**
 * Start the main event loop
 * @param loop Pointer to the event loop struct
 * @return Return code
 */
int
pgagroal_ev_loop(struct ev_loop* loop);

/**
 * Break the event loop, stopping its execution
 * @param loop Pointer to the event loop struct
 */
void
pgagroal_ev_loop_break(struct ev_loop* loop);

/**
 * Destroy the event loop, freeing only the strictly necessary resources that
 * need to be freed.
 *
 * @param loop Pointer to the event loop struct
 * @return Return code
 */
int
pgagroal_ev_loop_destroy(struct ev_loop* loop);

/**
 * Closes the file descriptors used by the loop of the parent process.
 *
 * @param loop Pointer to the loop that should be freed by the child process
 * @return Return code
 */
int
pgagroal_ev_fork(struct ev_loop* loop);

/**
 * Check if the event loop is currently running
 * @param loop Pointer to the event loop struct
 * @return True if the loop is running, false otherwise
 */
bool
pgagroal_ev_loop_is_running(struct ev_loop* loop);

/**
 * Atomically check if the event loop is running
 * @param loop Pointer to the event loop struct
 * @return True if the loop is running, false otherwise
 */
bool
pgagroal_ev_atomic_loop_is_running(struct ev_loop* loop);

/**
 * Initialize the watcher for accept event
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @return Return code
 */
int
pgagroal_ev_io_accept_init(struct ev_io* w, int fd, io_cb cb);

/**
 * Initialize the watcher for receive events
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @return Return code
 */
int
pgagroal_ev_io_receive_init(struct ev_io* w, int fd, io_cb cb);

/**
 * Initialize the watcher for sending IO operations
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @param buf Pointer to the buffer to be sent
 * @param buf_len Length of the buffer to be sent
 * @return Return code
 */
int
pgagroal_ev_io_send_init(struct ev_io* w, int fd, io_cb cb, void* buf, int buf_len);

/**
 * Start the watcher for an IO event in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the io event watcher struct
 * @return Return code
 */
int
pgagroal_ev_io_start(struct ev_loop* loop, struct ev_io* w);

/**
 * Stop the watcher for an IO event in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the io event watcher struct
 * @return Return code
 */
int
pgagroal_ev_io_stop(struct ev_loop* loop, struct ev_io* w);

/**
 * Initialize the watcher for periodic timeout events
 * @param w Pointer to the periodic event watcher struct
 * @param cb Callback executed on timeout
 * @param msec Interval in milliseconds for the periodic event
 * @return Return code
 */
int
pgagroal_ev_periodic_init(struct ev_periodic* w, periodic_cb cb, int msec);

/**
 * Start the watcher for a periodic timeout in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the periodic event watcher struct
 * @return Return code
 */
int
pgagroal_ev_periodic_start(struct ev_loop* loop, struct ev_periodic* w);

/**
 * Stop the watcher for a periodic timeout in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the periodic event watcher struct
 * @return Return code
 */
int
pgagroal_ev_periodic_stop(struct ev_loop* loop, struct ev_periodic* w);

/**
 * Initialize the watcher for signal events
 * @param w Pointer to the signal event watcher struct
 * @param cb Callback executed when signal is received
 * @param signum Signal number to watch
 * @return Return code
 */
int
pgagroal_ev_signal_init(struct ev_signal* w, signal_cb cb, int signum);

/**
 * Start the watcher for a signal in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the signal event watcher struct
 * @return Return code
 */
int
pgagroal_ev_signal_start(struct ev_loop* loop, struct ev_signal* w);

/**
 * Stop the watcher for a signal in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the signal event watcher struct
 * @return Return code
 */
int
pgagroal_ev_signal_stop(struct ev_loop* loop, struct ev_signal* w);

#endif /* EV_H */
