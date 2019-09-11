/*
 * Copyright (C) 2019 Red Hat
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
#include <pgagroal.h>
#include <configuration.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <pool.h>
#include <shmem.h>
#include <utils.h>
#include <worker.h>

#define ZF_LOG_TAG "main"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX(a, b) \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a > _b ? _a : _b; })

static volatile int keep_running = 1;

static void accept_main_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_mgt_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void shutdown_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void coredump_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void idle_timeout_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void validation_cb(struct ev_loop *loop, ev_periodic *w, int revents);

struct accept_info
{
   struct ev_io io;
   int socket;
   void* shmem;
};

struct periodic_info
{
   struct ev_periodic periodic;
   void* shmem;
};

int
main(int argc, char **argv)
{
   int ret;
   void* shmem = NULL;
   struct ev_loop *loop;
   struct accept_info io_main[64];
   struct accept_info io_mgt;
   struct signal_info signal_watcher[6];
   struct periodic_info idle_timeout;
   struct periodic_info validation;
   int* fds = NULL;
   int length;
   int unix_socket;
   size_t size;
   struct configuration* config;

   size = sizeof(struct configuration);
   shmem = pgagroal_create_shared_memory(size);
   pgagroal_init_configuration(shmem, size);
   config = (struct configuration*)shmem;
   
   ret = pgagroal_read_configuration("pgagroal.conf", shmem);
   if (ret)
      ret = pgagroal_read_configuration("/etc/pgagroal.conf", shmem);
   if (ret)
   {
      printf("pgagroal: Configuration not found\n");
      exit(1);
   }

   ret = pgagroal_read_hba_configuration("pgagroal_hba.conf", shmem);
   if (ret)
      ret = pgagroal_read_hba_configuration("/etc/pgagroal_hba.conf", shmem);
   if (ret)
   {
      printf("pgagroal: HBA configuration not found\n");
      exit(1);
   }

   pgagroal_start_logging(shmem);
   pgagroal_pool_init(shmem);

   /* Bind Unix Domain Socket for file descriptor transfers */
   unix_socket = pgagroal_bind_unix_socket(config->unix_socket_dir, shmem);

   /* Bind main socket */
   if (pgagroal_bind(config->host, config->port, shmem, &fds, &length))
   {
      printf("Could not bind to %s:%d\n", config->host, config->port);
      exit(1);
   }
   
   /* libev */
   loop = ev_default_loop(pgagroal_libev(config->libev));
   if (!loop)
   {
      printf("libev issue: No default loop implementation\n");
      exit(1);
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);
   ev_signal_init((struct ev_signal*)&signal_watcher[1], shutdown_cb, SIGHUP);
   ev_signal_init((struct ev_signal*)&signal_watcher[2], shutdown_cb, SIGINT);
   ev_signal_init((struct ev_signal*)&signal_watcher[3], shutdown_cb, SIGTRAP);
   ev_signal_init((struct ev_signal*)&signal_watcher[4], coredump_cb, SIGABRT);
   ev_signal_init((struct ev_signal*)&signal_watcher[5], shutdown_cb, SIGALRM);

   for (int i = 0; i < 6; i++)
   {
      signal_watcher[i].shmem = shmem;
      signal_watcher[i].slot = -1;
      ev_signal_start(loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_socket, EV_READ);
   io_mgt.socket = unix_socket;
   io_mgt.shmem = shmem;
   ev_io_start(loop, (struct ev_io*)&io_mgt);

   for (int i = 0; i < length; i++)
   {
      int sockfd = *(fds + i);
      ev_io_init((struct ev_io*)&io_main[i], accept_main_cb, sockfd, EV_READ);
      io_main[i].socket = sockfd;
      io_main[i].shmem = shmem;
      ev_io_start(loop, (struct ev_io*)&io_main[i]);
   }

   if (config->idle_timeout > 0)
   {
      ev_periodic_init ((struct ev_periodic*)&idle_timeout, idle_timeout_cb, 0.,
                        MAX(1. * config->idle_timeout / 2., 5.), 0);
      idle_timeout.shmem = shmem;
      ev_periodic_start (loop, (struct ev_periodic*)&idle_timeout);
   }

   if (config->validation == VALIDATION_BACKGROUND)
   {
      ev_periodic_init ((struct ev_periodic*)&validation, validation_cb, 0.,
                        MAX(1. * config->background_interval, 5.), 0);
      validation.shmem = shmem;
      ev_periodic_start (loop, (struct ev_periodic*)&validation);
   }

   ZF_LOGI("pgagroal: started on %s:%d", config->host, config->port);
   for (int i = 0; i < length; i++)
   {
      ZF_LOGD("Socket %d", *(fds + i));
   }
   ZF_LOGD("Management %d", unix_socket);
   pgagroal_libev_engines();
   ZF_LOGD("libev engine: %s", pgagroal_libev_engine(ev_backend(loop)));
   
   while (keep_running)
   {
      ev_loop(loop, 0);
   }

   ZF_LOGI("pgagroal: shutdown");
   pgagroal_pool_shutdown(shmem);
   ev_io_stop(loop, (struct ev_io*)&io_mgt);

   for (int i = 0; i < length; i++)
   {
      ev_io_stop(loop, (struct ev_io*)&io_main[i]);
      pgagroal_disconnect(io_main[i].socket);
   }

   for (int i = 0; i < 6; i++)
   {
      ev_signal_stop(loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(loop);

   pgagroal_disconnect(unix_socket);
   
   free(fds);

   pgagroal_stop_logging(shmem);
   pgagroal_destroy_shared_memory(shmem, size);

   return 0;
}

static void
accept_main_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   struct sockaddr_in client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct accept_info* ai;

   ZF_LOGV("accept_main_cb: sockfd ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      ZF_LOGD("accept_main_cb: invalid event: %s", strerror(errno));
      return;
   }

   memset(&address, 0, sizeof(address));

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      ZF_LOGD("accept_main_cb: accept: %s", strerror(errno));
      return;
   }

   ai = (struct accept_info*)watcher;

   pgagroal_get_address((struct sockaddr *)&client_addr, (char*)&address, sizeof(address));

   ZF_LOGV("accept_main_cb: client address: %s", address);

   if (!fork())
   {
      char* addr = malloc(sizeof(address));
      memcpy(addr, address, sizeof(address));

      ev_loop_fork(loop);
      pgagroal_disconnect(ai->socket);
      pgagroal_worker(client_fd, addr, ai->shmem);
   }

   pgagroal_disconnect(client_fd);
}

static void
accept_mgt_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   struct sockaddr_in client_addr;
   socklen_t client_addr_length;
   int client_fd;
   signed char id;
   int32_t slot;
   int payload;
   struct accept_info* ai;
   struct configuration* config;

   ZF_LOGV("pgagroal: unix_socket ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      perror("got invalid event");
      errno = 0;
      return;
   }

   ai = (struct accept_info*)watcher;
   config = (struct configuration*)ai->shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      perror("accept");
      errno = 0;
      return;
   }

   /* Process internal management request -- f.ex. returning a file descriptor to the pool */
   pgagroal_management_read_header(client_fd, &id, &slot);
   pgagroal_management_read_payload(client_fd, id, &payload);

   switch (id)
   {
      case MANAGEMENT_TRANSFER_CONNECTION:
         ZF_LOGD("pgagroal: Management transfer connection: Slot %d FD %d", slot, payload);
         config->connections[slot].fd = payload;
         break;
      case MANAGEMENT_KILL_CONNECTION:
         ZF_LOGD("pgagroal: Management kill connection: Slot %d", slot);
         pgagroal_disconnect(config->connections[slot].fd);
         break;
      case MANAGEMENT_FLUSH:
         ZF_LOGD("pgagroal: Management flush (%d)", payload);
         pgagroal_flush(ai->shmem, payload);
         break;
      default:
         ZF_LOGD("pgagroal: Unknown management id: %d", id);
         break;
   }

   /* pgagroal_management_free_payload(payload); */
   pgagroal_disconnect(client_fd);
}

static void
shutdown_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   struct signal_info* si;

   si = (struct signal_info*)w;

   ZF_LOGD("pgagroal: shutdown requested");

   pgagroal_pool_status(si->shmem);
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
coredump_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   struct signal_info* si;

   si = (struct signal_info*)w;

   ZF_LOGI("pgagroal: core dump requested");

   pgagroal_pool_status(si->shmem);
   abort();
}

static void
idle_timeout_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   struct periodic_info* pi;

   ZF_LOGV("pgagroal: idle_timeout_cb (%d)", revents);

   if (EV_ERROR & revents)
   {
      perror("got invalid event");
      errno = 0;
      return;
   }

   pi = (struct periodic_info*)w;

   /* pgagroal_idle_timeout() is always in a fork() */
   if (!fork())
   {
      pgagroal_idle_timeout(pi->shmem);
   }
}

static void
validation_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   struct periodic_info* pi;

   ZF_LOGV("pgagroal: validation_cb (%d)", revents);

   if (EV_ERROR & revents)
   {
      perror("got invalid event");
      errno = 0;
      return;
   }

   pi = (struct periodic_info*)w;

   /* pgagroal_validation() is always in a fork() */
   if (!fork())
   {
      pgagroal_validation(pi->shmem);
   }
}
