/*
 * Copyright (C) 2020 Red Hat
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
#include <message.h>
#include <pipeline.h>
#include <worker.h>

#define ZF_LOG_TAG "pipeline_session"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <stdlib.h>

static void* session_initialize(void*);
static void session_start(struct worker_io*);
static void session_client(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void session_server(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void session_stop(struct worker_io*);
static void session_destroy(void*);

struct pipeline session_pipeline()
{
   struct pipeline pipeline;

   pipeline.initialize = &session_initialize;
   pipeline.start = &session_start;
   pipeline.client = &session_client;
   pipeline.server = &session_server;
   pipeline.stop = &session_stop;
   pipeline.destroy = &session_destroy;

   return pipeline;
}

static void*
session_initialize(void* shmem)
{
   return NULL;
}

static void
session_start(struct worker_io* w)
{
}

static void
session_stop(struct worker_io* w)
{
}

static void
session_destroy(void* pointer)
{
}

static void
session_client(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   int status = MESSAGE_STATUS_ERROR;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;

   wi = (struct worker_io*)watcher;

   if (wi->client_ssl == NULL)
   {
      status = pgagroal_read_socket_message(wi->client_fd, &msg);
   }
   else
   {
      status = pgagroal_read_ssl_message(wi->client_ssl, &msg);
   }
   if (likely(status == MESSAGE_STATUS_OK))
   {
      if (likely(msg->kind != 'X'))
      {
         status = pgagroal_write_socket_message(wi->server_fd, msg);
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

   ev_break (loop, EVBREAK_ONE);
   return;

client_error:
   ZF_LOGD("client_fd %d - %s (%d)", wi->client_fd, strerror(errno), status);

   exit_code = WORKER_CLIENT_FAILURE;
   running = 0;
   ev_break(loop, EVBREAK_ALL);
   return;

server_error:
   ZF_LOGD("server_fd %d - %s (%d)", wi->server_fd, strerror(errno), status);

   exit_code = WORKER_SERVER_FAILURE;
   running = 0;
   ev_break(loop, EVBREAK_ALL);
   return;
}

static void
session_server(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   int status = MESSAGE_STATUS_ERROR;
   bool fatal = false;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;

   wi = (struct worker_io*)watcher;

   status = pgagroal_read_socket_message(wi->server_fd, &msg);
   if (likely(status == MESSAGE_STATUS_OK))
   {
      if (wi->client_ssl == NULL)
      {
         status = pgagroal_write_socket_message(wi->client_fd, msg);
      }
      else
      {
         status = pgagroal_write_ssl_message(wi->client_ssl, msg);
      }
      if (unlikely(status != MESSAGE_STATUS_OK))
      {
         goto client_error;
      }

      if (unlikely(msg->kind == 'E'))
      {
         fatal = false;

         if (!strncmp(msg->data + 6, "FATAL", 5) || !strncmp(msg->data + 6, "PANIC", 5))
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

   ev_break(loop, EVBREAK_ONE);
   return;

client_error:
   ZF_LOGD("client_fd %d - %s (%d)", wi->client_fd, strerror(errno), status);

   exit_code = WORKER_CLIENT_FAILURE;
   running = 0;
   ev_break(loop, EVBREAK_ALL);
   return;

server_error:
   ZF_LOGD("server_fd %d - %s (%d)", wi->server_fd, strerror(errno), status);

   exit_code = WORKER_SERVER_FAILURE;
   running = 0;
   ev_break(loop, EVBREAK_ALL);
   return;
}
