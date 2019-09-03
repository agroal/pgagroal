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
static void sigint_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void idle_timeout_cb(struct ev_loop *loop, ev_periodic *w, int revents);

struct accept_info
{
   struct ev_io io;
   int socket;
   void* shmem;
};

struct idle_timeout_info
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
   ev_signal signal_watcher;
   struct idle_timeout_info idle_timeout;
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

   ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
   ev_signal_start(loop, &signal_watcher);

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
   struct accept_info* ai;

   ZF_LOGV("pgagroal: sockfd ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      perror("got invalid event");
      errno = 0;
      return;
   }

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      perror("accept");
      errno = 0;
      return;
   }

   ai = (struct accept_info*)watcher;

   /* Verify 's' against pgagroal_hba.conf
      inet_ntop(client_addr.ss_family,
      pgagroal_get_sockaddr((struct sockaddr *)&client_addr),
      s, sizeof(s));
   */

   if (!fork())
   {
      ev_loop_fork(loop);
      pgagroal_disconnect(ai->socket);
      pgagroal_worker(client_fd, ai->shmem);
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
sigint_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
idle_timeout_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   struct idle_timeout_info* iti;

   ZF_LOGV("pgagroal: idle_timeout_cb (%d)", revents);

   if (EV_ERROR & revents)
   {
      perror("got invalid event");
      errno = 0;
      return;
   }

   iti = (struct idle_timeout_info*)w;

   /* pgagroal_idle_timeout() is always in a fork() */
   if (!fork())
   {
      pgagroal_idle_timeout(iti->shmem);
   }
}
