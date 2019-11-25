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
#include <pipeline.h>
#include <pool.h>
#include <shmem.h>
#include <utils.h>
#include <worker.h>

#define ZF_LOG_TAG "main"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_FDS 64
#define MAX(a, b)               \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a > _b ? _a : _b; })

static void accept_main_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_mgt_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void shutdown_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void graceful_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void coredump_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void idle_timeout_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void validation_cb(struct ev_loop *loop, ev_periodic *w, int revents);

struct accept_io
{
   struct ev_io io;
   int socket;
   void* shmem;
   void* pipeline_shmem;
};

struct periodic_info
{
   struct ev_periodic periodic;
   void* shmem;
};

static volatile int keep_running = 1;
static bool gracefully = false;
static struct accept_io io_main[MAX_FDS];
static int length;

static void
shutdown_io(struct ev_loop *loop)
{
   for (int i = 0; i < length; i++)
   {
      ev_io_stop(loop, (struct ev_io*)&io_main[i]);
      pgagroal_shutdown(io_main[i].socket);
      pgagroal_disconnect(io_main[i].socket);
   }
}

static void
version()
{
   printf("pgagroal %s\n", VERSION);
   exit(1);
}

static void
usage()
{
   printf("pgagroal %s\n", VERSION);
   printf("  High-performance connection pool for PostgreSQL\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal [ -c CONFIG_FILE ] [ -a HBA_CONFIG_FILE ] [ -l LIMIT_CONFIG_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE      Set the path to the pgagroal.conf file\n");
   printf("  -a, --hba HBA_CONFIG_FILE     Set the path to the pgagroal_hba.conf file\n");
   printf("  -l, --limit LIMIT_CONFIG_FILE Set the path to the pgagroal_databases.conf file\n");
   printf("  -d, --daemon                  Run as a daemon\n");
   printf("  -V, --version                 Display version information\n");
   printf("  -?, --help                    Display help\n");
   printf("\n");

   exit(1);
}

int
main(int argc, char **argv)
{
   char* configuration_path = NULL;
   char* hba_path = NULL;
   char* limit_path = NULL;
   bool daemon = false;
   pid_t pid, sid;
   void* shmem = NULL;
   void* tmp_shmem = NULL;
   struct ev_loop *loop;
   struct accept_io io_mgt;
   struct signal_info signal_watcher[6];
   struct periodic_info idle_timeout;
   struct periodic_info validation;
   int* fds = NULL;
   int unix_socket;
   size_t size;
   size_t tmp_size;
   struct configuration* config;
   int c;
   struct pipeline p;
   void* pipeline_shmem = NULL;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config",  required_argument, 0, 'c'},
         {"hba", required_argument, 0, 'a'},
         {"limit", required_argument, 0, 'l'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };
      int option_index = 0;

      c = getopt_long (argc, argv, "dV?a:c:l:",
                       long_options, &option_index);

      if (c == -1)
         break;

      switch (c)
      {
         case 'a':
            hba_path = optarg;
            break;
         case 'c':
            configuration_path = optarg;
            break;
         case 'l':
            limit_path = optarg;
            break;
         case 'd':
            daemon = true;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            break;
         default:
            break;
      }
   }

   if (getuid() == 0)
   {
      printf("pgagroal: Using the root account is not allowed\n");
      exit(1);
   }

   size = sizeof(struct configuration);
   shmem = pgagroal_create_shared_memory(size);
   pgagroal_init_configuration(shmem, size);

   if (configuration_path != NULL)
   {
      if (pgagroal_read_configuration(configuration_path, shmem))
      {
         printf("pgagroal: Configuration not found: %s\n", configuration_path);
         exit(1);
      }
   }
   else
   {
      if (pgagroal_read_configuration("/etc/pgagroal.conf", shmem))
      {
         printf("pgagroal: Configuration not found: /etc/pgagroal.conf\n");
         exit(1);
      }
   }

   if (hba_path != NULL)
   {
      if (pgagroal_read_hba_configuration(hba_path, shmem))
      {
         printf("pgagroal: HBA configuration not found: %s\n", hba_path);
         exit(1);
      }
   }
   else
   {
      if (pgagroal_read_hba_configuration("/etc/pgagroal_hba.conf", shmem))
      {
         printf("pgagroal: HBA configuration not found: /etc/pgagroal_hba.conf\n");
         exit(1);
      }
   }

   if (limit_path != NULL)
   {
      if (pgagroal_read_limit_configuration(limit_path, shmem))
      {
         printf("pgagroal: LIMIT configuration not found: %s\n", limit_path);
         exit(1);
      }
   }
   else
   {
      pgagroal_read_limit_configuration("/etc/pgagroal_databases.conf", shmem);
   }

   if (pgagroal_validate_configuration(shmem))
   {
      exit(1);
   }
   if (pgagroal_validate_hba_configuration(shmem))
   {
      exit(1);
   }
   if (pgagroal_validate_limit_configuration(shmem))
   {
      exit(1);
   }

   pgagroal_resize_shared_memory(size, shmem, &tmp_size, &tmp_shmem);
   pgagroal_destroy_shared_memory(shmem, size);
   size = tmp_size;
   shmem = tmp_shmem;

   config = (struct configuration*)shmem;

   if (daemon)
   {
      if (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
      {
         printf("pgagroal: Daemon mode can't be used with console logging\n");
         exit(1);
      }

      pid = fork();

      if (pid < 0)
      {
         printf("pgagroal: Daemon mode failed\n");
         exit(1);
      }

      if (pid > 0)
      {
         exit(0);
      }

      /* We are a daemon now */
      umask(0);
      sid = setsid();

      if (sid < 0)
      {
         exit(1);
      }
   }

   pgagroal_start_logging(shmem);
   pgagroal_pool_init(shmem);

   /* Bind Unix Domain Socket for file descriptor transfers */
   unix_socket = pgagroal_bind_unix_socket(config->unix_socket_dir, shmem);

   /* Bind main socket */
   if (pgagroal_bind(config->host, config->port, shmem, &fds, &length))
   {
      printf("pgagroal: Could not bind to %s:%d\n", config->host, config->port);
      exit(1);
   }
   
   if (length > MAX_FDS)
   {
      printf("pgagroal: Too many descriptors %d\n", length);
      exit(1);
   }

   /* libev */
   loop = ev_default_loop(pgagroal_libev(config->libev));
   if (!loop)
   {
      printf("pgagroal: No loop implementation (%x) (%x)\n",
             pgagroal_libev(config->libev), ev_supported_backends());
      exit(1);
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);
   ev_signal_init((struct ev_signal*)&signal_watcher[1], shutdown_cb, SIGHUP);
   ev_signal_init((struct ev_signal*)&signal_watcher[2], shutdown_cb, SIGINT);
   ev_signal_init((struct ev_signal*)&signal_watcher[3], graceful_cb, SIGTRAP);
   ev_signal_init((struct ev_signal*)&signal_watcher[4], coredump_cb, SIGABRT);
   ev_signal_init((struct ev_signal*)&signal_watcher[5], shutdown_cb, SIGALRM);

   for (int i = 0; i < 6; i++)
   {
      signal_watcher[i].shmem = shmem;
      signal_watcher[i].slot = -1;
      ev_signal_start(loop, (struct ev_signal*)&signal_watcher[i]);
   }

   p = performance_pipeline();
   pipeline_shmem = p.initialize(shmem);

   ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_socket, EV_READ);
   io_mgt.socket = unix_socket;
   io_mgt.shmem = shmem;
   io_mgt.pipeline_shmem = pipeline_shmem;
   ev_io_start(loop, (struct ev_io*)&io_mgt);

   for (int i = 0; i < length; i++)
   {
      int sockfd = *(fds + i);
      ev_io_init((struct ev_io*)&io_main[i], accept_main_cb, sockfd, EV_READ);
      io_main[i].socket = sockfd;
      io_main[i].shmem = shmem;
      io_main[i].pipeline_shmem = pipeline_shmem;
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
   ZF_LOGD("Configuration size: %lu", size);
   ZF_LOGD("Max connections: %d", config->max_connections);

   while (keep_running)
   {
      ev_loop(loop, 0);
   }

   ZF_LOGI("pgagroal: shutdown");
   pgagroal_pool_shutdown(shmem);
   ev_io_stop(loop, (struct ev_io*)&io_mgt);

   if (!gracefully)
   {
      shutdown_io(loop);
   }

   for (int i = 0; i < 6; i++)
   {
      ev_signal_stop(loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(loop);

   pgagroal_disconnect(unix_socket);
   
   free(fds);

   p.destroy(pipeline_shmem);

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
   struct accept_io* ai;

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

   ai = (struct accept_io*)watcher;

   pgagroal_get_address((struct sockaddr *)&client_addr, (char*)&address, sizeof(address));

   ZF_LOGV("accept_main_cb: client address: %s", address);

   if (!fork())
   {
      char* addr = malloc(sizeof(address));
      memcpy(addr, address, sizeof(address));

      ev_loop_fork(loop);
      pgagroal_disconnect(ai->socket);
      pgagroal_worker(client_fd, addr, ai->shmem, ai->pipeline_shmem);
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
   struct accept_io* ai;
   struct configuration* config;

   ZF_LOGV("pgagroal: unix_socket ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      ZF_LOGV("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   ai = (struct accept_io*)watcher;
   config = (struct configuration*)ai->shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      ZF_LOGV("accept_mgt_cb: accept: %s", strerror(errno));
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
      case MANAGEMENT_RETURN_CONNECTION:
         ZF_LOGD("pgagroal: Management return connection: Slot %d", slot);
         break;
      case MANAGEMENT_KILL_CONNECTION:
         ZF_LOGD("pgagroal: Management kill connection: Slot %d", slot);
         pgagroal_disconnect(config->connections[slot].fd);
         break;
      case MANAGEMENT_FLUSH:
         ZF_LOGD("pgagroal: Management flush (%d)", payload);
         pgagroal_flush(ai->shmem, payload);
         break;
      case MANAGEMENT_GRACEFULLY:
         ZF_LOGD("pgagroal: Management gracefully");
         pgagroal_pool_status(ai->shmem);
         gracefully = true;
         shutdown_io(loop);
         break;
      case MANAGEMENT_STOP:
         ZF_LOGD("pgagroal: Management stop");
         pgagroal_pool_status(ai->shmem);
         ev_break(loop, EVBREAK_ALL);
         keep_running = 0;
         break;
      case MANAGEMENT_STATUS:
         ZF_LOGD("pgagroal: Management status");
         pgagroal_pool_status(ai->shmem);
         pgagroal_management_write_status(gracefully, ai->shmem, client_fd);
         break;
      case MANAGEMENT_DETAILS:
         ZF_LOGD("pgagroal: Management details");
         pgagroal_pool_status(ai->shmem);
         pgagroal_management_write_status(gracefully, ai->shmem, client_fd);
         pgagroal_management_write_details(ai->shmem, client_fd);
         break;
      default:
         ZF_LOGD("pgagroal: Unknown management id: %d", id);
         break;
   }

   if (keep_running && gracefully)
   {
      if (atomic_load(&config->active_connections) == 0)
      {
         pgagroal_pool_status(ai->shmem);
         keep_running = 0;
         ev_break(loop, EVBREAK_ALL);
      }
   }

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
graceful_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   struct signal_info* si;
   struct configuration* config;

   si = (struct signal_info*)w;
   config = (struct configuration*)si->shmem;

   ZF_LOGD("pgagroal: gracefully requested");

   pgagroal_pool_status(si->shmem);
   gracefully = true;
   shutdown_io(loop);

   if (atomic_load(&config->active_connections) == 0)
   {
      pgagroal_pool_status(si->shmem);
      keep_running = 0;
      ev_break(loop, EVBREAK_ALL);
   }
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
      ZF_LOGV("idle_timeout_cb: got invalid event: %s", strerror(errno));
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
      ZF_LOGV("validation_cb: got invalid event: %s", strerror(errno));
      return;
   }

   pi = (struct periodic_info*)w;

   /* pgagroal_validation() is always in a fork() */
   if (!fork())
   {
      pgagroal_validation(pi->shmem);
   }
}
