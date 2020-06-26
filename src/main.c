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
#include <configuration.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <pipeline.h>
#include <pool.h>
#include <prometheus.h>
#include <remote.h>
#include <security.h>
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
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <openssl/crypto.h>

#define MAX_FDS 64

static void accept_main_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_mgt_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_metrics_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_management_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void shutdown_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void graceful_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void coredump_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void idle_timeout_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void validation_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void disconnect_client_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static bool accept_fatal(int error);

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
   void* pipeline_shmem;
};

static volatile int keep_running = 1;
static struct ev_loop* main_loop = NULL;
static struct accept_io io_main[MAX_FDS];
static struct accept_io io_mgt;
static int* main_fds = NULL;
static int main_fds_length = -1;
static int unix_socket = -1;
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static struct accept_io io_management[MAX_FDS];
static int* management_fds = NULL;
static int management_fds_length = -1;
static struct pipeline main_pipeline;
static void* shmem = NULL;
static void* pipeline_shmem = NULL;
static int known_fds[MAX_NUMBER_OF_CONNECTIONS];

static void
start_mgt()
{
   memset(&io_mgt, 0, sizeof(struct accept_io));
   ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_socket, EV_READ);
   io_mgt.socket = unix_socket;
   io_mgt.shmem = shmem;
   io_mgt.pipeline_shmem = pipeline_shmem;
   ev_io_start(main_loop, (struct ev_io*)&io_mgt);
}

static void
shutdown_mgt()
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   ev_io_stop(main_loop, (struct ev_io*)&io_mgt);
   pgagroal_disconnect(unix_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir);
   errno = 0;
}

static void
start_io()
{
   for (int i = 0; i < main_fds_length; i++)
   {
      int sockfd = *(main_fds + i);

      memset(&io_main[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_main[i], accept_main_cb, sockfd, EV_READ);
      io_main[i].socket = sockfd;
      io_main[i].shmem = shmem;
      io_main[i].pipeline_shmem = pipeline_shmem;
      ev_io_start(main_loop, (struct ev_io*)&io_main[i]);
   }
}

static void
shutdown_io()
{
   for (int i = 0; i < main_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_main[i]);
      pgagroal_disconnect(io_main[i].socket);
      errno = 0;
   }
}

static void
start_metrics()
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      int sockfd = *(metrics_fds + i);

      memset(&io_metrics[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_metrics[i], accept_metrics_cb, sockfd, EV_READ);
      io_metrics[i].socket = sockfd;
      io_metrics[i].shmem = shmem;
      io_metrics[i].pipeline_shmem = pipeline_shmem;
      ev_io_start(main_loop, (struct ev_io*)&io_metrics[i]);
   }
}

static void
shutdown_metrics()
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_metrics[i]);
      pgagroal_disconnect(io_metrics[i].socket);
      errno = 0;
   }
}

static void
start_management()
{
   for (int i = 0; i < management_fds_length; i++)
   {
      int sockfd = *(management_fds + i);

      memset(&io_management[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_management[i], accept_management_cb, sockfd, EV_READ);
      io_management[i].socket = sockfd;
      io_management[i].shmem = shmem;
      io_management[i].pipeline_shmem = pipeline_shmem;
      ev_io_start(main_loop, (struct ev_io*)&io_management[i]);
   }
}

static void
shutdown_management()
{
   for (int i = 0; i < management_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_management[i]);
      pgagroal_disconnect(io_management[i].socket);
      errno = 0;
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
   printf("  pgagroal [ -c CONFIG_FILE ] [ -a HBA_CONFIG_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE       Set the path to the pgagroal.conf file\n");
   printf("  -a, --hba HBA_CONFIG_FILE      Set the path to the pgagroal_hba.conf file\n");
   printf("  -l, --limit LIMIT_CONFIG_FILE  Set the path to the pgagroal_databases.conf file\n");
   printf("  -u, --users USERS_FILE         Set the path to the pgagroal_users.conf file\n");
   printf("  -A, --admins ADMINS_FILE       Set the path to the pgagroal_admins.conf file\n");
   printf("  -S, --superuser SUPERUSER_FILE Set the path to the pgagroal_superuser.conf file\n");
   printf("  -d, --daemon                   Run as a daemon\n");
   printf("  -V, --version                  Display version information\n");
   printf("  -?, --help                     Display help\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char **argv)
{
   char* configuration_path = NULL;
   char* hba_path = NULL;
   char* limit_path = NULL;
   char* users_path = NULL;
   char* admins_path = NULL;
   char* superuser_path = NULL;
   bool daemon = false;
   pid_t pid, sid;
   void* tmp_shmem = NULL;
   struct signal_info signal_watcher[6];
   struct periodic_info idle_timeout;
   struct periodic_info validation;
   struct periodic_info disconnect_client;
   struct rlimit flimit;
   size_t shmem_size;
   size_t pipeline_shmem_size = 0;
   size_t tmp_size;
   struct configuration* config = NULL;
   int ret;
   int c;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config",  required_argument, 0, 'c'},
         {"hba", required_argument, 0, 'a'},
         {"limit", required_argument, 0, 'l'},
         {"users", required_argument, 0, 'u'},
         {"admins", required_argument, 0, 'A'},
         {"superuser", required_argument, 0, 'S'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };
      int option_index = 0;

      c = getopt_long (argc, argv, "dV?a:c:l:u:A:S:",
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
         case 'u':
            users_path = optarg;
            break;
         case 'A':
            admins_path = optarg;
            break;
         case 'S':
            superuser_path = optarg;
            break;
         case 'd':
            daemon = true;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(1);
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

   shmem_size = sizeof(struct configuration);
   shmem = pgagroal_create_shared_memory(shmem_size);
   pgagroal_init_configuration(shmem, shmem_size);

   memset(&known_fds, 0, sizeof(known_fds));

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
      if (pgagroal_read_configuration("/etc/pgagroal/pgagroal.conf", shmem))
      {
         printf("pgagroal: Configuration not found: /etc/pgagroal/pgagroal.conf\n");
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
      if (pgagroal_read_hba_configuration("/etc/pgagroal/pgagroal_hba.conf", shmem))
      {
         printf("pgagroal: HBA configuration not found: /etc/pgagroal/pgagroal_hba.conf\n");
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
      pgagroal_read_limit_configuration("/etc/pgagroal/pgagroal_databases.conf", shmem);
   }

   if (users_path != NULL)
   {
      ret = pgagroal_read_users_configuration(users_path, shmem);
      if (ret == 1)
      {
         printf("pgagroal: USERS configuration not found: %s\n", users_path);
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: USERS: Too many users defined %d (max %d)\n", config->number_of_users, NUMBER_OF_USERS);
         exit(1);
      }
   }
   else
   {
      pgagroal_read_users_configuration("/etc/pgagroal/pgagroal_users.conf", shmem);
   }

   if (admins_path != NULL)
   {
      ret = pgagroal_read_admins_configuration(admins_path, shmem);
      if (ret == 1)
      {
         printf("pgagroal: ADMINS configuration not found: %s\n", admins_path);
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: ADMINS: Too many admins defined %d (max %d)\n", config->number_of_admins, NUMBER_OF_ADMINS);
         exit(1);
      }
   }
   else
   {
      pgagroal_read_users_configuration("/etc/pgagroal/pgagroal_admins.conf", shmem);
   }

   if (superuser_path != NULL)
   {
      ret = pgagroal_read_superuser_configuration(superuser_path, shmem);
      if (ret == 1)
      {
         printf("pgagroal: SUPERUSER configuration not found: %s\n", superuser_path);
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: SUPERUSER: Too many superusers defined (max 1)\n");
         exit(1);
      }
   }
   else
   {
      pgagroal_read_users_configuration("/etc/pgagroal/pgagroal_superuser.conf", shmem);
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
   if (pgagroal_validate_users_configuration(shmem))
   {
      exit(1);
   }
   if (pgagroal_validate_admins_configuration(shmem))
   {
      exit(1);
   }

   pgagroal_resize_shared_memory(shmem_size, shmem, &tmp_size, &tmp_shmem);
   pgagroal_destroy_shared_memory(shmem, shmem_size);
   shmem_size = tmp_size;
   shmem = tmp_shmem;

   config = (struct configuration*)shmem;

   if (getrlimit(RLIMIT_NOFILE, &flimit) == -1)
   {
      printf("pgagroal: Unable to find limit due to %s\n", strerror(errno));
      exit(1);
   }

   /* We are "reserving" 30 file descriptors for pgagroal main */
   if (config->max_connections > (flimit.rlim_cur - 30))
   {
      printf("pgagroal: max_connections is larger than the number of available file descriptors for connections (%ld)\n", flimit.rlim_cur - 30);
      exit(1);
   }

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
   if (pgagroal_bind_unix_socket(config->unix_socket_dir, shmem, &unix_socket))
   {
      ZF_LOGF("pgagroal: Could not bind to %s\n", config->unix_socket_dir);
      exit(1);
   }

   /* Bind main socket */
   if (pgagroal_bind(config->host, config->port, shmem, &main_fds, &main_fds_length))
   {
      ZF_LOGF("pgagroal: Could not bind to %s:%d\n", config->host, config->port);
      exit(1);
   }

   if (main_fds_length > MAX_FDS)
   {
      ZF_LOGF("pgagroal: Too many descriptors %d\n", main_fds_length);
      exit(1);
   }

   /* libev */
   main_loop = ev_default_loop(pgagroal_libev(config->libev));
   if (!main_loop)
   {
      ZF_LOGF("pgagroal: No loop implementation (%x) (%x)\n",
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
      ev_signal_start(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   if (config->pipeline == PIPELINE_PERFORMANCE)
   {
      main_pipeline = performance_pipeline();
   }
   else if (config->pipeline == PIPELINE_SESSION)
   {
      if (pgagroal_tls_valid(shmem))
      {
         ZF_LOGF("pgagroal: Invalid TLS configuration");
         exit(1);
      }

      main_pipeline = session_pipeline();
   }
   else
   {
      ZF_LOGF("pgagroal: Unknown pipeline identifier (%d)", config->pipeline);
      exit(1);
   }

   if (main_pipeline.initialize(shmem, &pipeline_shmem, &pipeline_shmem_size))
   {
      ZF_LOGF("pgagroal: Pipeline initialize error (%d)", config->pipeline);
      exit(1);
   }

   start_mgt();
   start_io();

   if (config->idle_timeout > 0)
   {
      ev_periodic_init ((struct ev_periodic*)&idle_timeout, idle_timeout_cb, 0.,
                        MAX(1. * config->idle_timeout / 2., 5.), 0);
      idle_timeout.shmem = shmem;
      idle_timeout.pipeline_shmem = pipeline_shmem;
      ev_periodic_start (main_loop, (struct ev_periodic*)&idle_timeout);
   }

   if (config->validation == VALIDATION_BACKGROUND)
   {
      ev_periodic_init ((struct ev_periodic*)&validation, validation_cb, 0.,
                        MAX(1. * config->background_interval, 5.), 0);
      validation.shmem = shmem;
      validation.pipeline_shmem = pipeline_shmem;
      ev_periodic_start (main_loop, (struct ev_periodic*)&validation);
   }

   if (config->disconnect_client > 0)
   {
      ev_periodic_init ((struct ev_periodic*)&disconnect_client, disconnect_client_cb, 0.,
                        MAX(1. * config->disconnect_client / 2., 1.), 0);
      disconnect_client.shmem = shmem;
      disconnect_client.pipeline_shmem = pipeline_shmem;
      ev_periodic_start (main_loop, (struct ev_periodic*)&disconnect_client);
   }

   if (config->metrics > 0)
   {
      /* Bind metrics socket */
      if (pgagroal_bind(config->host, config->metrics, shmem, &metrics_fds, &metrics_fds_length))
      {
         ZF_LOGF("pgagroal: Could not bind to %s:%d\n", config->host, config->metrics);
         exit(1);
      }

      if (metrics_fds_length > MAX_FDS)
      {
         ZF_LOGF("pgagroal: Too many descriptors %d\n", metrics_fds_length);
         exit(1);
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgagroal_bind(config->host, config->management, shmem, &management_fds, &management_fds_length))
      {
         ZF_LOGF("pgagroal: Could not bind to %s:%d\n", config->host, config->management);
         exit(1);
      }

      if (management_fds_length > MAX_FDS)
      {
         ZF_LOGF("pgagroal: Too many descriptors %d\n", management_fds_length);
         exit(1);
      }

      start_management();
   }

   ZF_LOGI("pgagroal: started on %s:%d", config->host, config->port);
   for (int i = 0; i < main_fds_length; i++)
   {
      ZF_LOGD("Socket: %d", *(main_fds + i));
   }
   ZF_LOGD("Management: %d", unix_socket);
   for (int i = 0; i < metrics_fds_length; i++)
   {
      ZF_LOGD("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      ZF_LOGD("Remote management: %d", *(management_fds + i));
   }
   pgagroal_libev_engines();
   ZF_LOGD("libev engine: %s", pgagroal_libev_engine(ev_backend(main_loop)));
   ZF_LOGD("Pipeline: %d", config->pipeline);
   ZF_LOGD("Pipeline size: %lu", pipeline_shmem_size);
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   ZF_LOGD("%s", SSLeay_version(SSLEAY_VERSION));
#else
   ZF_LOGD("%s", OpenSSL_version(OPENSSL_VERSION));
#endif
   ZF_LOGD("Configuration size: %lu", shmem_size);
   ZF_LOGD("Max connections: %d", config->max_connections);
   ZF_LOGD("Known users: %d", config->number_of_users);
   ZF_LOGD("Known admins: %d", config->number_of_admins);
   ZF_LOGD("Known superuser: %s", strlen(config->superuser.username) > 0 ? "Yes" : "No");

   if (!config->allow_unknown_users && config->number_of_users == 0)
   {
      ZF_LOGW("No users allowed");
   }

   if (config->number_of_users > 0)
   {
      if (!fork())
      {
         pgagroal_prefill(shmem, true);
      }
   }

   while (keep_running)
   {
      ev_loop(main_loop, 0);
   }

   ZF_LOGI("pgagroal: shutdown");
   pgagroal_pool_shutdown(shmem);

   shutdown_management();
   shutdown_metrics();
   shutdown_mgt();
   shutdown_io();

   for (int i = 0; i < 6; i++)
   {
      ev_signal_stop(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(main_loop);

   free(main_fds);

   main_pipeline.destroy(pipeline_shmem, pipeline_shmem_size);

   pgagroal_stop_logging(shmem);
   pgagroal_destroy_shared_memory(shmem, shmem_size);

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
   struct configuration* config;

   ZF_LOGV("accept_main_cb: sockfd ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      ZF_LOGD("accept_main_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   ai = (struct accept_io*)watcher;
   config = (struct configuration*)ai->shmem;

   memset(&address, 0, sizeof(address));

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         ZF_LOGW("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_io();

         free(main_fds);
         main_fds = NULL;
         main_fds_length = 0;

         if (pgagroal_bind(config->host, config->port, ai->shmem, &main_fds, &main_fds_length))
         {
            ZF_LOGF("pgagroal: Could not bind to %s:%d\n", config->host, config->port);
            exit(1);
         }

         if (main_fds_length > MAX_FDS)
         {
            ZF_LOGF("pgagroal: Too many descriptors %d\n", main_fds_length);
            exit(1);
         }

         if (!fork())
         {
            pgagroal_flush(ai->shmem, FLUSH_GRACEFULLY);
         }

         start_io();

         for (int i = 0; i < main_fds_length; i++)
         {
            ZF_LOGD("Socket: %d", *(main_fds + i));
         }
      }
      else
      {
         ZF_LOGD("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pgagroal_get_address((struct sockaddr *)&client_addr, (char*)&address, sizeof(address));

   ZF_LOGV("accept_main_cb: client address: %s", address);

   if (!fork())
   {
      char* addr = malloc(sizeof(address));
      memcpy(addr, address, sizeof(address));

      ev_loop_fork(loop);
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
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
   int payload_i;
   char* payload_s = NULL;
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
      if (accept_fatal(errno) && keep_running)
      {
         ZF_LOGW("Restarting management due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, shmem, &unix_socket))
         {
            ZF_LOGF("pgagroal: Could not bind to %s\n", config->unix_socket_dir);
            exit(1);
         }

         start_mgt();

         ZF_LOGD("Management: %d", unix_socket);
      }
      else
      {
         ZF_LOGD("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   /* Process internal management request -- f.ex. returning a file descriptor to the pool */
   pgagroal_management_read_header(client_fd, &id, &slot);
   pgagroal_management_read_payload(client_fd, id, &payload_i, &payload_s);

   switch (id)
   {
      case MANAGEMENT_TRANSFER_CONNECTION:
         ZF_LOGD("pgagroal: Management transfer connection: Slot %d FD %d", slot, payload_i);
         config->connections[slot].fd = payload_i;
         known_fds[slot] = config->connections[slot].fd;
         break;
      case MANAGEMENT_RETURN_CONNECTION:
         ZF_LOGD("pgagroal: Management return connection: Slot %d", slot);
         break;
      case MANAGEMENT_KILL_CONNECTION:
         ZF_LOGD("pgagroal: Management kill connection: Slot %d", slot);
         if (known_fds[slot] == payload_i)
         {
            pgagroal_disconnect(payload_i);
            known_fds[slot] = 0;
         }
         break;
      case MANAGEMENT_FLUSH:
         ZF_LOGD("pgagroal: Management flush (%d)", payload_i);
         if (!fork())
         {
            pgagroal_flush(ai->shmem, payload_i);
         }
         break;
      case MANAGEMENT_ENABLEDB:
         ZF_LOGD("pgagroal: Management enabledb: %s", payload_s);
         pgagroal_pool_status(ai->shmem);

         for (int i = 0; i < NUMBER_OF_DISABLED; i++)
         {
            if (!strcmp("*", payload_s))
            {
               memset(&config->disabled[i], 0, MAX_DATABASE_LENGTH);
            }
            else if (!strcmp(config->disabled[i], payload_s))
            {
               memset(&config->disabled[i], 0, MAX_DATABASE_LENGTH);
            }
         }

         free(payload_s);
         break;
      case MANAGEMENT_DISABLEDB:
         ZF_LOGD("pgagroal: Management disabledb: %s", payload_s);
         pgagroal_pool_status(ai->shmem);

         if (!strcmp("*", payload_s))
         {
            for (int i = 0; i < NUMBER_OF_DISABLED; i++)
            {
               memset(&config->disabled[i], 0, MAX_DATABASE_LENGTH);
            }

            memcpy(&config->disabled[0], payload_s, 1);
         }
         else
         {
            for (int i = 0; i < NUMBER_OF_DISABLED; i++)
            {
               if (!strcmp(config->disabled[i], ""))
               {
                  memcpy(&config->disabled[i], payload_s, strlen(payload_s));
                  break;
               }
            }
         }

         free(payload_s);
         break;
      case MANAGEMENT_GRACEFULLY:
         ZF_LOGD("pgagroal: Management gracefully");
         pgagroal_pool_status(ai->shmem);
         config->gracefully = true;
         break;
      case MANAGEMENT_STOP:
         ZF_LOGD("pgagroal: Management stop");
         pgagroal_pool_status(ai->shmem);
         ev_break(loop, EVBREAK_ALL);
         keep_running = 0;
         break;
      case MANAGEMENT_CANCEL_SHUTDOWN:
         ZF_LOGD("pgagroal: Management cancel shutdown");
         pgagroal_pool_status(ai->shmem);
         config->gracefully = false;
         break;
      case MANAGEMENT_STATUS:
         ZF_LOGD("pgagroal: Management status");
         pgagroal_pool_status(ai->shmem);
         pgagroal_management_write_status(client_fd, config->gracefully, ai->shmem);
         break;
      case MANAGEMENT_DETAILS:
         ZF_LOGD("pgagroal: Management details");
         pgagroal_pool_status(ai->shmem);
         pgagroal_management_write_status(client_fd, config->gracefully, ai->shmem);
         pgagroal_management_write_details(client_fd, ai->shmem);
         break;
      case MANAGEMENT_ISALIVE:
         ZF_LOGD("pgagroal: Management isalive");
         pgagroal_management_write_isalive(client_fd, config->gracefully, ai->shmem);
         break;
      case MANAGEMENT_RESET:
         ZF_LOGD("pgagroal: Management reset");
         pgagroal_prometheus_reset(ai->shmem);
         break;
      default:
         ZF_LOGD("pgagroal: Unknown management id: %d", id);
         break;
   }

   if (keep_running && config->gracefully)
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
accept_metrics_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   struct sockaddr_in client_addr;
   socklen_t client_addr_length;
   int client_fd;
   struct accept_io* ai;
   struct configuration* config;

   ZF_LOGV("accept_metrics_cb: sockfd ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      ZF_LOGD("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   ai = (struct accept_io*)watcher;
   config = (struct configuration*)ai->shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         ZF_LOGW("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_metrics();

         free(metrics_fds);
         metrics_fds = NULL;
         metrics_fds_length = 0;

         if (pgagroal_bind(config->host, config->port, ai->shmem, &metrics_fds, &metrics_fds_length))
         {
            ZF_LOGF("pgagroal: Could not bind to %s:%d\n", config->host, config->port);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            ZF_LOGF("pgagroal: Too many descriptors %d\n", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            ZF_LOGD("Metrics: %d", *(metrics_fds + i));
         }
      }
      else
      {
         ZF_LOGD("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   if (!fork())
   {
      ev_loop_fork(loop);
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_prometheus(client_fd, ai->shmem, ai->pipeline_shmem);
   }

   pgagroal_disconnect(client_fd);
}

static void
accept_management_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   struct sockaddr_in client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct accept_io* ai;
   struct configuration* config;

   ZF_LOGV("accept_management_cb: sockfd ready (%d)", revents);

   if (EV_ERROR & revents)
   {
      ZF_LOGD("accept_management_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   memset(&address, 0, sizeof(address));

   ai = (struct accept_io*)watcher;
   config = (struct configuration*)ai->shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         ZF_LOGW("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_management();

         free(management_fds);
         management_fds = NULL;
         management_fds_length = 0;

         if (pgagroal_bind(config->host, config->port, ai->shmem, &management_fds, &management_fds_length))
         {
            ZF_LOGF("pgagroal: Could not bind to %s:%d\n", config->host, config->port);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            ZF_LOGF("pgagroal: Too many descriptors %d\n", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            ZF_LOGD("Remote management: %d", *(management_fds + i));
         }
      }
      else
      {
         ZF_LOGD("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pgagroal_get_address((struct sockaddr *)&client_addr, (char*)&address, sizeof(address));

   if (!fork())
   {
      char* addr = malloc(sizeof(address));
      memcpy(addr, address, sizeof(address));

      ev_loop_fork(loop);
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_remote_management(client_fd, addr, ai->shmem, ai->pipeline_shmem);
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
   config->gracefully = true;

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

static void
disconnect_client_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   struct periodic_info* pi;

   ZF_LOGV("pgagroal: disconnect_client_cb (%d)", revents);

   if (EV_ERROR & revents)
   {
      ZF_LOGV("disconnect_client_cb: got invalid event: %s", strerror(errno));
      return;
   }

   pi = (struct periodic_info*)w;

   /* main_pipeline.periodic is always in a fork() */
   if (!fork())
   {
      main_pipeline.periodic(pi->shmem, pi->pipeline_shmem);
   }
}

static bool
accept_fatal(int error)
{
   switch (error)
   {
      case EAGAIN:
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
      case ENONET:
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
         return false;
         break;
   }

   return true;
}
