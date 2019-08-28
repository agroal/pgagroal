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
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <pool.h>
#include <security.h>
#include <worker.h>
#include <utils.h>

#define ZF_LOG_TAG "worker"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WORKER_SUCCESS        0
#define WORKER_CLIENT_FAILURE 1
#define WORKER_SERVER_FAILURE 2
#define WORKER_SERVER_FATAL   3

static volatile int running = 1;
static volatile int exit_code = WORKER_SUCCESS;

static void client_pgagroal_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void server_pgagroal_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);

struct worker_info
{
   struct ev_io io;
   int client_fd;
   int server_fd;
};

void
pgagroal_worker(int client_fd, void* shmem)
{
   struct ev_loop *loop = NULL;
   struct worker_info client_io = {0};
   struct worker_info server_io = {0};
   struct configuration* config = NULL;
   int32_t slot = -1;

   pgagroal_start_logging(shmem);
   pgagroal_memory_init(shmem);

   config = (struct configuration*)shmem;

   /* Authentication */
   if (pgagroal_authenticate(client_fd, shmem, &slot) == AUTH_SUCCESS)
   {
      ZF_LOGD("pgagroal_worker: Slot %d (%d -> %d)", slot, client_fd, config->connections[slot].fd);
      pgagroal_pool_status(shmem);

      if (config->nodelay)
      {
         if (pgagroal_tcp_nodelay(client_fd, shmem))
         {
            ZF_LOGW("pgagroal_worker: TCP_NODELAY failed for %d", client_fd);
         }
      }
      
      if (config->non_blocking)
      {
         if (pgagroal_socket_nonblocking(client_fd, shmem))
         {
            ZF_LOGW("pgagroal_worker: O_NONBLOCK failed for %d", client_fd);
         }
      }
      
      if (pgagroal_socket_buffers(client_fd, shmem))
      {
         ZF_LOGW("pgagroal_worker: SO_RCVBUF/SO_SNDBUF failed for %d", client_fd);
      }
      
      ev_io_init((struct ev_io*)&client_io, client_pgagroal_cb, client_fd, EV_READ);
      client_io.client_fd = client_fd;
      client_io.server_fd = config->connections[slot].fd;
      
      ev_io_init((struct ev_io*)&server_io, server_pgagroal_cb, config->connections[slot].fd, EV_READ);
      server_io.client_fd = client_fd;
      server_io.server_fd = config->connections[slot].fd;
      
      loop = ev_loop_new(EVFLAG_AUTO);
      ev_io_start(loop, (struct ev_io*)&client_io);
      ev_io_start(loop, (struct ev_io*)&server_io);

      while (running)
      {
         ev_loop(loop, 0);
      }
   }

   /* Return to pool */
   if (slot != -1)
   {
      if (exit_code == WORKER_SUCCESS || exit_code == WORKER_CLIENT_FAILURE)
      {
         pgagroal_return_connection(shmem, slot);
      }
      else
      {
         pgagroal_kill_connection(shmem, slot);
      }
   }

   ZF_LOGD("client disconnect: %d", client_fd);
   pgagroal_disconnect(client_fd);

   ZF_LOGD("After client");
   pgagroal_pool_status(shmem);

   if (loop)
   {
      ev_io_stop(loop, (struct ev_io*)&client_io);
      ev_io_stop(loop, (struct ev_io*)&server_io);
      ev_loop_destroy(loop);
   }

   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(exit_code);
}

static void
client_pgagroal_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   int status = MESSAGE_STATUS_ERROR;
   struct worker_info* wi = NULL;
   struct message* msg = NULL;

   wi = (struct worker_info*)watcher;

   status = pgagroal_read_message(wi->client_fd, &msg);
   if (likely(status == MESSAGE_STATUS_OK))
   {
      if (likely(msg->kind != 'X'))
      {
         status = pgagroal_write_message(wi->server_fd, msg);
         if (unlikely(status != MESSAGE_STATUS_OK))
         {
            goto server_error;
         }
      }
      else if (msg->kind == 'X')
      {
         exit_code = WORKER_SUCCESS;
         running = 0;
      }
   }
   else
   {
      goto client_error;
   }

   /* We don't need to "free" the memory for the message */
   /* pgagroal_free_message(msg); */

   ev_break (loop, EVBREAK_ONE);
   return;

client_error:
   ZF_LOGD("client_fd %d - %s (%d)", wi->client_fd, strerror(errno), status);

   ev_break (loop, EVBREAK_ONE);
   exit_code = WORKER_CLIENT_FAILURE;
   running = 0;
   return;

server_error:
   ZF_LOGD("server_fd %d - %s (%d)", wi->server_fd, strerror(errno), status);

   ev_break (loop, EVBREAK_ONE);
   exit_code = WORKER_SERVER_FAILURE;
   running = 0;
   return;
}

static void
server_pgagroal_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   int status = MESSAGE_STATUS_ERROR;
   bool fatal = false;
   struct worker_info* wi = NULL;
   struct message* msg = NULL;

   wi = (struct worker_info*)watcher;

   status = pgagroal_read_message(wi->server_fd, &msg);
   if (likely(status == MESSAGE_STATUS_OK))
   {
      status = pgagroal_write_message(wi->client_fd, msg);
      if (unlikely(status != MESSAGE_STATUS_OK))
      {
         goto client_error;
      }

      if (unlikely(msg->kind == 'E'))
      {
         fatal = false;

         if (!strncmp(msg->data + 6, "FATAL", 5))
            fatal = true;

         if (fatal)
         {
            exit_code = WORKER_SERVER_FATAL;
            running = 0;
         }
      }
   }
   else
   {
      goto server_error;
   }

   /* We don't need to "free" the memory for the message */
   /* pgagroal_free_message(msg); */

   ev_break (loop, EVBREAK_ONE);
   return;

client_error:
   ZF_LOGD("client_fd %d - %s (%d)", wi->client_fd, strerror(errno), status);

   ev_break (loop, EVBREAK_ONE);
   exit_code = WORKER_CLIENT_FAILURE;
   running = 0;
   return;

server_error:
   ZF_LOGD("server_fd %d - %s (%d)", wi->server_fd, strerror(errno), status);

   ev_break (loop, EVBREAK_ONE);
   exit_code = WORKER_SERVER_FAILURE;
   running = 0;
   return;
}
