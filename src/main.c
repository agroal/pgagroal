/*
 * Copyright (C) 2022 Red Hat
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
#include <server.h>
#include <shmem.h>
#include <utils.h>
#include <worker.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <openssl/crypto.h>
#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif

#define MAX_FDS 64

static void accept_main_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void reload_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void graceful_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void coredump_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void idle_timeout_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void validation_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void disconnect_client_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static bool accept_fatal(int error);
static void add_client(pid_t pid);
static void remove_client(pid_t pid);
static void reload_configuration(void);
static int  create_pidfile(void);
static void remove_pidfile(void);
static void shutdown_ports(void);

struct accept_io
{
   struct ev_io io;
   int socket;
   char** argv;
};

struct client
{
   pid_t pid;
   struct client* next;
};

static volatile int keep_running = 1;
static char** argv_ptr;
static struct ev_loop* main_loop = NULL;
static struct accept_io io_main[MAX_FDS];
static struct accept_io io_mgt;
static struct accept_io io_uds;
static int* main_fds = NULL;
static int main_fds_length = -1;
static int unix_management_socket = -1;
static int unix_pgsql_socket = -1;
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static struct accept_io io_management[MAX_FDS];
static int* management_fds = NULL;
static int management_fds_length = -1;
static struct pipeline main_pipeline;
static int known_fds[MAX_NUMBER_OF_CONNECTIONS];
static struct client* clients = NULL;

static void
start_mgt(void)
{
   memset(&io_mgt, 0, sizeof(struct accept_io));
   ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_management_socket, EV_READ);
   io_mgt.socket = unix_management_socket;
   io_mgt.argv = argv_ptr;
   ev_io_start(main_loop, (struct ev_io*)&io_mgt);
}

static void
shutdown_mgt(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   ev_io_stop(main_loop, (struct ev_io*)&io_mgt);
   pgagroal_disconnect(unix_management_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir, MAIN_UDS);
   errno = 0;
}

static void
start_uds(void)
{
   memset(&io_uds, 0, sizeof(struct accept_io));
   ev_io_init((struct ev_io*)&io_uds, accept_main_cb, unix_pgsql_socket, EV_READ);
   io_uds.socket = unix_pgsql_socket;
   io_uds.argv = argv_ptr;
   ev_io_start(main_loop, (struct ev_io*)&io_uds);
}

static void
shutdown_uds(void)
{
   char pgsql[MISC_LENGTH];
   struct configuration* config;

   config = (struct configuration*)shmem;

   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->port);

   ev_io_stop(main_loop, (struct ev_io*)&io_uds);
   pgagroal_disconnect(unix_pgsql_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir, &pgsql[0]);
   errno = 0;
}

static void
start_io(void)
{
   for (int i = 0; i < main_fds_length; i++)
   {
      int sockfd = *(main_fds + i);

      memset(&io_main[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_main[i], accept_main_cb, sockfd, EV_READ);
      io_main[i].socket = sockfd;
      io_main[i].argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_main[i]);
   }
}

static void
shutdown_io(void)
{
   for (int i = 0; i < main_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_main[i]);
      pgagroal_disconnect(io_main[i].socket);
      errno = 0;
   }
}

static void
start_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      int sockfd = *(metrics_fds + i);

      memset(&io_metrics[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_metrics[i], accept_metrics_cb, sockfd, EV_READ);
      io_metrics[i].socket = sockfd;
      io_metrics[i].argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_metrics[i]);
   }
}

static void
shutdown_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_metrics[i]);
      pgagroal_disconnect(io_metrics[i].socket);
      errno = 0;
   }
}

static void
start_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      int sockfd = *(management_fds + i);

      memset(&io_management[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_management[i], accept_management_cb, sockfd, EV_READ);
      io_management[i].socket = sockfd;
      io_management[i].argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_management[i]);
   }
}

static void
shutdown_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_management[i]);
      pgagroal_disconnect(io_management[i].socket);
      errno = 0;
   }
}

static void
version(void)
{
   printf("pgagroal %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal %s\n", VERSION);
   printf("  High-performance connection pool for PostgreSQL\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal [ -c CONFIG_FILE ] [ -a HBA_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE           Set the path to the pgagroal.conf file\n");
   printf("  -a, --hba HBA_FILE                 Set the path to the pgagroal_hba.conf file\n");
   printf("  -l, --limit LIMIT_FILE             Set the path to the pgagroal_databases.conf file\n");
   printf("  -u, --users USERS_FILE             Set the path to the pgagroal_users.conf file\n");
   printf("  -F, --frontend FRONTEND_USERS_FILE Set the path to the pgagroal_frontend_users.conf file\n");
   printf("  -A, --admins ADMINS_FILE           Set the path to the pgagroal_admins.conf file\n");
   printf("  -S, --superuser SUPERUSER_FILE     Set the path to the pgagroal_superuser.conf file\n");
   printf("  -d, --daemon                       Run as a daemon\n");
   printf("  -V, --version                      Display version information\n");
   printf("  -?, --help                         Display help\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char** argv)
{
   char* configuration_path = NULL;
   char* hba_path = NULL;
   char* limit_path = NULL;
   char* users_path = NULL;
   char* frontend_users_path = NULL;
   char* admins_path = NULL;
   char* superuser_path = NULL;
   bool daemon = false;
   pid_t pid, sid;
#ifdef HAVE_LINUX
   int sds;
#endif
   bool has_unix_socket = false;
   bool has_main_sockets = false;
   void* tmp_shmem = NULL;
   struct signal_info signal_watcher[6];
   struct ev_periodic idle_timeout;
   struct ev_periodic validation;
   struct ev_periodic disconnect_client;
   struct rlimit flimit;
   size_t shmem_size;
   size_t pipeline_shmem_size = 0;
   size_t prometheus_shmem_size = 0;
   size_t prometheus_cache_shmem_size = 0;
   size_t tmp_size;
   struct configuration* config = NULL;
   int ret;
   int c;

   argv_ptr = argv;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"hba", required_argument, 0, 'a'},
         {"limit", required_argument, 0, 'l'},
         {"users", required_argument, 0, 'u'},
         {"frontend", required_argument, 0, 'F'},
         {"admins", required_argument, 0, 'A'},
         {"superuser", required_argument, 0, 'S'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };
      int option_index = 0;

      c = getopt_long (argc, argv, "dV?a:c:l:u:F:A:S:",
                       long_options, &option_index);

      if (c == -1)
      {
         break;
      }

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
         case 'F':
            frontend_users_path = optarg;
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
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      exit(1);
   }

   shmem_size = sizeof(struct configuration);
   if (pgagroal_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
      printf("pgagroal: Error in creating shared memory\n");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      exit(1);
   }

   pgagroal_init_configuration(shmem);
   config = (struct configuration*)shmem;

   memset(&known_fds, 0, sizeof(known_fds));

   if (configuration_path != NULL)
   {
      if (pgagroal_read_configuration(shmem, configuration_path, true))
      {
         printf("pgagroal: Configuration not found: %s\n", configuration_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Configuration not found: %s", configuration_path);
#endif
         exit(1);
      }
   }
   else
   {
      if (pgagroal_read_configuration(shmem, "/etc/pgagroal/pgagroal.conf", true))
      {
         printf("pgagroal: Configuration not found: /etc/pgagroal/pgagroal.conf\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Configuration not found: /etc/pgagroal/pgagroal.conf");
#endif
         exit(1);
      }
      configuration_path = "/etc/pgagroal/pgagroal.conf";
   }
   memcpy(&config->configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   if (hba_path != NULL)
   {
      if (pgagroal_read_hba_configuration(shmem, hba_path))
      {
         printf("pgagroal: HBA configuration not found: %s\n", hba_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=HBA configuration not found: %s", hba_path);
#endif
         exit(1);
      }
   }
   else
   {
      if (pgagroal_read_hba_configuration(shmem, "/etc/pgagroal/pgagroal_hba.conf"))
      {
         printf("pgagroal: HBA configuration not found: /etc/pgagroal/pgagroal_hba.conf\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=HBA configuration not found: /etc/pgagroal/pgagroal_hba.conf");
#endif
         exit(1);
      }
      hba_path = "/etc/pgagroal/pgagroal_hba.conf";
   }
   memcpy(&config->hba_path[0], hba_path, MIN(strlen(hba_path), MAX_PATH - 1));

   if (limit_path != NULL)
   {
      if (pgagroal_read_limit_configuration(shmem, limit_path))
      {
         printf("pgagroal: LIMIT configuration not found: %s\n", limit_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=LIMIT configuration not found: %s", limit_path);
#endif
         exit(1);
      }
      memcpy(&config->limit_path[0], limit_path, MIN(strlen(limit_path), MAX_PATH - 1));
   }
   else
   {
      limit_path = "/etc/pgagroal/pgagroal_databases.conf";
      ret = pgagroal_read_limit_configuration(shmem, limit_path);
      if (ret == 0)
      {
         memcpy(&config->limit_path[0], limit_path, MIN(strlen(limit_path), MAX_PATH - 1));
      }
   }

   if (users_path != NULL)
   {
      ret = pgagroal_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         printf("pgagroal: USERS configuration not found: %s\n", users_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=USERS configuration not found: %s", users_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: USERS: Too many users defined %d (max %d)\n", config->number_of_users, NUMBER_OF_USERS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=USERS: Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_USERS);
#endif
         exit(1);
      }
      memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
   }
   else
   {
      users_path = "/etc/pgagroal/pgagroal_users.conf";
      ret = pgagroal_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
   }

   if (frontend_users_path != NULL)
   {
      ret = pgagroal_read_frontend_users_configuration(shmem, frontend_users_path);
      if (ret == 1)
      {
         printf("pgagroal: FRONTEND USERS configuration not found: %s\n", frontend_users_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=FRONTEND USERS configuration not found: %s", frontend_users_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: FRONTEND USERS: Too many users defined %d (max %d)\n", config->number_of_frontend_users, NUMBER_OF_USERS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=FRONTEND USERS: Too many users defined %d (max %d)", config->number_of_frontend_users, NUMBER_OF_USERS);
#endif
         exit(1);
      }
      memcpy(&config->frontend_users_path[0], frontend_users_path, MIN(strlen(frontend_users_path), MAX_PATH - 1));
   }
   else
   {
      frontend_users_path = "/etc/pgagroal/pgagroal_frontend_users.conf";
      ret = pgagroal_read_frontend_users_configuration(shmem, frontend_users_path);
      if (ret == 0)
      {
         memcpy(&config->frontend_users_path[0], frontend_users_path, MIN(strlen(frontend_users_path), MAX_PATH - 1));
      }
   }

   if (admins_path != NULL)
   {
      ret = pgagroal_read_admins_configuration(shmem, admins_path);
      if (ret == 1)
      {
         printf("pgagroal: ADMINS configuration not found: %s\n", admins_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=ADMINS configuration not found: %s", admins_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: ADMINS: Too many admins defined %d (max %d)\n", config->number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=ADMINS: Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#endif
         exit(1);
      }
      memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
   }
   else
   {
      admins_path = "/etc/pgagroal/pgagroal_admins.conf";
      ret = pgagroal_read_admins_configuration(shmem, admins_path);
      if (ret == 0)
      {
         memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
      }
   }

   if (superuser_path != NULL)
   {
      ret = pgagroal_read_superuser_configuration(shmem, superuser_path);
      if (ret == 1)
      {
         printf("pgagroal: SUPERUSER configuration not found: %s\n", superuser_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=SUPERUSER configuration not found: %s", superuser_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgagroal: Invalid master key file\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgagroal: SUPERUSER: Too many superusers defined (max 1)\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=SUPERUSER: Too many superusers defined (max 1)");
#endif
         exit(1);
      }
      memcpy(&config->superuser_path[0], superuser_path, MIN(strlen(superuser_path), MAX_PATH - 1));
   }
   else
   {
      superuser_path = "/etc/pgagroal/pgagroal_superuser.conf";
      ret = pgagroal_read_superuser_configuration(shmem, superuser_path);
      if (ret == 0)
      {
         memcpy(&config->superuser_path[0], superuser_path, MIN(strlen(superuser_path), MAX_PATH - 1));
      }
   }

   /* systemd sockets */
#ifdef HAVE_LINUX
   sds = sd_listen_fds(0);
   if (sds > 0)
   {
      int m = 0;

      main_fds_length = 0;

      for (int i = 0; i < sds; i++)
      {
         int fd = SD_LISTEN_FDS_START + i;

         if (sd_is_socket(fd, AF_INET, 0, -1) || sd_is_socket(fd, AF_INET6, 0, -1))
         {
            main_fds_length++;
         }
      }

      if (main_fds_length > 0)
      {
         main_fds = malloc(main_fds_length * sizeof(int));
      }

      for (int i = 0; i < sds; i++)
      {
         int fd = SD_LISTEN_FDS_START + i;

         if (sd_is_socket(fd, AF_UNIX, 0, -1))
         {
            unix_pgsql_socket = fd;
            has_unix_socket = true;
         }
         else if (sd_is_socket(fd, AF_INET, 0, -1) || sd_is_socket(fd, AF_INET6, 0, -1))
         {
            *(main_fds + (m * sizeof(int))) = fd;
            has_main_sockets = true;
            m++;
         }
      }
   }
#endif

   if (pgagroal_init_logging())
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Failed to init logging");
#endif
      exit(1);
   }

   if (pgagroal_start_logging())
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      exit(1);
   }

   if (pgagroal_validate_configuration(shmem, has_unix_socket, has_main_sockets))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      exit(1);
   }
   if (pgagroal_validate_hba_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid HBA configuration");
#endif
      exit(1);
   }
   if (pgagroal_validate_limit_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid LIMIT configuration");
#endif
      exit(1);
   }
   if (pgagroal_validate_users_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      exit(1);
   }
   if (pgagroal_validate_frontend_users_configuration(shmem))
   {
      printf("pgagroal: Invalid FRONTEND USERS configuration\n");
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid FRONTEND USERS configuration");
#endif
      exit(1);
   }
   if (pgagroal_validate_admins_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      exit(1);
   }

   if (pgagroal_resize_shared_memory(shmem_size, shmem, &tmp_size, &tmp_shmem))
   {
      printf("pgagroal: Error in creating shared memory\n");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      exit(1);
   }
   if (pgagroal_destroy_shared_memory(shmem, shmem_size) == -1)
   {
      printf("pgagroal: Error in destroying shared memory\n");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in destroying shared memory");
#endif
      exit(1);
   }
   shmem_size = tmp_size;
   shmem = tmp_shmem;
   config = (struct configuration*)shmem;

   if (pgagroal_init_prometheus(&prometheus_shmem_size, &prometheus_shmem))
   {
      printf("pgagroal: Error in creating and initializing prometheus shared memory\n");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus shared memory");
#endif
      exit(1);
   }

   if (pgagroal_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
   {
      printf("pgagroal: Error in creating and initializing prometheus cache shared memory\n");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
      exit(1);
   }

   if (getrlimit(RLIMIT_NOFILE, &flimit) == -1)
   {
      printf("pgagroal: Unable to find limit due to %s\n", strerror(errno));
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Unable to find limit due to %s", strerror(errno));
#endif
      exit(1);
   }

   /* We are "reserving" 30 file descriptors for pgagroal main */
   if (config->max_connections > (flimit.rlim_cur - 30))
   {
      printf("pgagroal: max_connections is larger than the file descriptor limit (%ld available)\n", flimit.rlim_cur - 30);
#ifdef HAVE_LINUX
      sd_notifyf(0,
                 "STATUS=max_connections is larger than the file descriptor limit (%ld available)",
                 flimit.rlim_cur - 30);
#endif
      exit(1);
   }

   if (daemon)
   {
      if (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
      {
         printf("pgagroal: Daemon mode can't be used with console logging\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         exit(1);
      }

      pid = fork();

      if (pid < 0)
      {
         printf("pgagroal: Daemon mode failed\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode failed");
#endif
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

   if (create_pidfile())
   {
      exit(1);
   }

   pgagroal_pool_init();

   pgagroal_set_proc_title(argc, argv, "main", NULL);

   /* Bind Unix Domain Socket for file descriptor transfers */
   if (pgagroal_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#endif
      exit(1);
   }

   if (!has_unix_socket)
   {
      char pgsql[MISC_LENGTH];

      memset(&pgsql, 0, sizeof(pgsql));
      snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->port);

      if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
#endif
         exit(1);
      }
   }

   /* Bind main socket */
   if (!has_main_sockets)
   {
      if (pgagroal_bind(config->host, config->port, &main_fds, &main_fds_length))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->port);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->port);
#endif
         exit(1);
      }
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Too many descriptors %d", main_fds_length);
#endif
      exit(1);
   }

   /* libev */
   main_loop = ev_default_loop(pgagroal_libev(config->libev));
   if (!main_loop)
   {
      pgagroal_log_fatal("pgagroal: No loop implementation (%x) (%x)",
                         pgagroal_libev(config->libev), ev_supported_backends());
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=No loop implementation (%x) (%x)", pgagroal_libev(config->libev), ev_supported_backends());
#endif
      exit(1);
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);
   ev_signal_init((struct ev_signal*)&signal_watcher[1], reload_cb, SIGHUP);
   ev_signal_init((struct ev_signal*)&signal_watcher[2], shutdown_cb, SIGINT);
   ev_signal_init((struct ev_signal*)&signal_watcher[3], graceful_cb, SIGTRAP);
   ev_signal_init((struct ev_signal*)&signal_watcher[4], coredump_cb, SIGABRT);
   ev_signal_init((struct ev_signal*)&signal_watcher[5], shutdown_cb, SIGALRM);

   for (int i = 0; i < 6; i++)
   {
      signal_watcher[i].slot = -1;
      ev_signal_start(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   if (config->pipeline == PIPELINE_PERFORMANCE)
   {
      main_pipeline = performance_pipeline();
   }
   else if (config->pipeline == PIPELINE_SESSION)
   {
      if (pgagroal_tls_valid())
      {
         pgagroal_log_fatal("pgagroal: Invalid TLS configuration");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
         exit(1);
      }

      main_pipeline = session_pipeline();
   }
   else if (config->pipeline == PIPELINE_TRANSACTION)
   {
      if (pgagroal_tls_valid())
      {
         pgagroal_log_fatal("pgagroal: Invalid TLS configuration");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
         exit(1);
      }

      main_pipeline = transaction_pipeline();
   }
   else
   {
      pgagroal_log_fatal("pgagroal: Unknown pipeline identifier (%d)", config->pipeline);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Unknown pipeline identifier (%d)", config->pipeline);
#endif
      exit(1);
   }

   if (main_pipeline.initialize(shmem, &pipeline_shmem, &pipeline_shmem_size))
   {
      pgagroal_log_fatal("pgagroal: Pipeline initialize error (%d)", config->pipeline);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Pipeline initialize error (%d)", config->pipeline);
#endif
      exit(1);
   }

   start_mgt();
   start_uds();
   start_io();

   if (config->idle_timeout > 0)
   {
      ev_periodic_init (&idle_timeout, idle_timeout_cb, 0.,
                        MAX(1. * config->idle_timeout / 2., 5.), 0);
      ev_periodic_start (main_loop, &idle_timeout);
   }

   if (config->validation == VALIDATION_BACKGROUND)
   {
      ev_periodic_init (&validation, validation_cb, 0.,
                        MAX(1. * config->background_interval, 5.), 0);
      ev_periodic_start (main_loop, &validation);
   }

   if (config->disconnect_client > 0)
   {
      ev_periodic_init (&disconnect_client, disconnect_client_cb, 0.,
                        MIN(300., MAX(1. * config->disconnect_client / 2., 1.)), 0);
      ev_periodic_start (main_loop, &disconnect_client);
   }

   if (config->metrics > 0)
   {
      /* Bind metrics socket */
      if (pgagroal_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->metrics);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->metrics);
#endif
         exit(1);
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         exit(1);
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgagroal_bind(config->host, config->management, &management_fds, &management_fds_length))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->management);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->management);
#endif
         exit(1);
      }

      if (management_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         exit(1);
      }

      start_management();
   }

   pgagroal_log_info("pgagroal: started on %s:%d", config->host, config->port);
   for (int i = 0; i < main_fds_length; i++)
   {
      pgagroal_log_debug("Socket: %d", *(main_fds + i));
   }
   pgagroal_log_debug("Unix Domain Socket: %d", unix_pgsql_socket);
   pgagroal_log_debug("Management: %d", unix_management_socket);
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgagroal_log_debug("Remote management: %d", *(management_fds + i));
   }
   pgagroal_libev_engines();
   pgagroal_log_debug("libev engine: %s", pgagroal_libev_engine(ev_backend(main_loop)));
   pgagroal_log_debug("Pipeline: %d", config->pipeline);
   pgagroal_log_debug("Pipeline size: %lu", pipeline_shmem_size);
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   pgagroal_log_debug("%s", SSLeay_version(SSLEAY_VERSION));
#else
   pgagroal_log_debug("%s", OpenSSL_version(OPENSSL_VERSION));
#endif
   pgagroal_log_debug("Configuration size: %lu", shmem_size);
   pgagroal_log_debug("Max connections: %d", config->max_connections);
   pgagroal_log_debug("Known users: %d", config->number_of_users);
   pgagroal_log_debug("Known frontend users: %d", config->number_of_frontend_users);
   pgagroal_log_debug("Known admins: %d", config->number_of_admins);
   pgagroal_log_debug("Known superuser: %s", strlen(config->superuser.username) > 0 ? "Yes" : "No");

   if (!config->allow_unknown_users && config->number_of_users == 0)
   {
      pgagroal_log_warn("No users allowed");
   }

   if (pgagroal_can_prefill())
   {
      if (!fork())
      {
         shutdown_ports();
         pgagroal_prefill_if_can(true);
      }
   }

#ifdef HAVE_LINUX
   sd_notifyf(0,
              "READY=1\n"
              "STATUS=Running\n"
              "MAINPID=%lu", (unsigned long)getpid());
#endif

   while (keep_running)
   {
      ev_loop(main_loop, 0);
   }

   pgagroal_log_info("pgagroal: shutdown");
#ifdef HAVE_LINUX
   sd_notify(0, "STOPPING=1");
#endif
   pgagroal_pool_shutdown();

   if (clients != NULL)
   {
      struct client* c = clients;
      while (c != NULL)
      {
         kill(c->pid, SIGQUIT);
         c = c->next;
      }
   }

   shutdown_management();
   shutdown_metrics();
   shutdown_mgt();
   shutdown_io();
   shutdown_uds();

   for (int i = 0; i < 6; i++)
   {
      ev_signal_stop(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(main_loop);

   free(main_fds);
   free(metrics_fds);
   free(management_fds);

   main_pipeline.destroy(pipeline_shmem, pipeline_shmem_size);

   remove_pidfile();

   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(prometheus_shmem, prometheus_shmem_size);
   pgagroal_destroy_shared_memory(prometheus_cache_shmem, prometheus_cache_shmem_size);
   pgagroal_destroy_shared_memory(shmem, shmem_size);

   return 0;
}

static void
accept_main_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   pid_t pid;
   struct accept_io* ai;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_main_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   ai = (struct accept_io*)watcher;
   config = (struct configuration*)shmem;

   memset(&address, 0, sizeof(address));

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         char pgsql[MISC_LENGTH];

         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_io();
         shutdown_uds();

         memset(&pgsql, 0, sizeof(pgsql));
         snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->port);

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
            exit(1);
         }

         free(main_fds);
         main_fds = NULL;
         main_fds_length = 0;

         if (pgagroal_bind(config->host, config->port, &main_fds, &main_fds_length))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->port);
            exit(1);
         }

         if (main_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
            exit(1);
         }

         if (!fork())
         {
            shutdown_ports();
            pgagroal_flush(FLUSH_GRACEFULLY, "*");
         }

         start_io();
         start_uds();

         for (int i = 0; i < main_fds_length; i++)
         {
            pgagroal_log_debug("Socket: %d", *(main_fds + i));
         }
         pgagroal_log_debug("Unix Domain Socket: %d", unix_pgsql_socket);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pgagroal_prometheus_client_sockets_add();

   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   pgagroal_log_trace("accept_main_cb: client address: %s", address);

   pid = fork();
   if (pid == -1)
   {
      /* No process */
      pgagroal_log_error("Cannot create process");
   }
   else if (pid > 0)
   {
      add_client(pid);
   }
   else
   {
      char* addr = malloc(strlen(address) + 1);
      memset(addr, 0, strlen(address) + 1);
      memcpy(addr, address, strlen(address));

      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_worker(client_fd, addr, ai->argv);
   }

   pgagroal_disconnect(client_fd);
}

static void
accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   signed char id;
   int32_t slot;
   int payload_i;
   char* payload_s = NULL;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct configuration*)shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgagroal_log_warn("Restarting management due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s", config->unix_socket_dir);
            exit(1);
         }

         start_mgt();

         pgagroal_log_debug("Management: %d", unix_management_socket);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
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
         pgagroal_log_debug("pgagroal: Management transfer connection: Slot %d FD %d", slot, payload_i);
         config->connections[slot].fd = payload_i;
         known_fds[slot] = config->connections[slot].fd;

         if (config->pipeline == PIPELINE_TRANSACTION)
         {
            struct client* c = clients;
            while (c != NULL)
            {
               pgagroal_management_client_fd(slot, c->pid);
               c = c->next;
            }
         }

         break;
      case MANAGEMENT_RETURN_CONNECTION:
         pgagroal_log_debug("pgagroal: Management return connection: Slot %d", slot);
         break;
      case MANAGEMENT_KILL_CONNECTION:
         pgagroal_log_debug("pgagroal: Management kill connection: Slot %d", slot);
         if (known_fds[slot] == payload_i)
         {
            struct client* c = clients;

            while (c != NULL)
            {
               pgagroal_management_remove_fd(slot, payload_i, c->pid);
               c = c->next;
            }

            pgagroal_disconnect(payload_i);
            known_fds[slot] = 0;
         }
         break;
      case MANAGEMENT_FLUSH:
         pgagroal_log_debug("pgagroal: Management flush (%d/%s)", payload_i, payload_s);
         if (!fork())
         {
            shutdown_ports();
            pgagroal_flush(payload_i, payload_s);
         }
         break;
      case MANAGEMENT_ENABLEDB:
         pgagroal_log_debug("pgagroal: Management enabledb: %s", payload_s);
         pgagroal_pool_status();

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
         pgagroal_log_debug("pgagroal: Management disabledb: %s", payload_s);
         pgagroal_pool_status();

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
         pgagroal_log_debug("pgagroal: Management gracefully");
         pgagroal_pool_status();
         config->gracefully = true;
         break;
      case MANAGEMENT_STOP:
         pgagroal_log_debug("pgagroal: Management stop");
         pgagroal_pool_status();
         ev_break(loop, EVBREAK_ALL);
         keep_running = 0;
         break;
      case MANAGEMENT_CANCEL_SHUTDOWN:
         pgagroal_log_debug("pgagroal: Management cancel shutdown");
         pgagroal_pool_status();
         config->gracefully = false;
         break;
      case MANAGEMENT_STATUS:
         pgagroal_log_debug("pgagroal: Management status");
         pgagroal_pool_status();
         pgagroal_management_write_status(client_fd, config->gracefully);
         break;
      case MANAGEMENT_DETAILS:
         pgagroal_log_debug("pgagroal: Management details");
         pgagroal_pool_status();
         pgagroal_management_write_status(client_fd, config->gracefully);
         pgagroal_management_write_details(client_fd);
         break;
      case MANAGEMENT_ISALIVE:
         pgagroal_log_debug("pgagroal: Management isalive");
         pgagroal_management_write_isalive(client_fd, config->gracefully);
         break;
      case MANAGEMENT_RESET:
         pgagroal_log_debug("pgagroal: Management reset");
         pgagroal_prometheus_reset();
         break;
      case MANAGEMENT_RESET_SERVER:
         pgagroal_log_debug("pgagroal: Management reset server");
         pgagroal_server_reset(payload_s);
         pgagroal_prometheus_failed_servers();
         break;
      case MANAGEMENT_CLIENT_DONE:
         pgagroal_log_debug("pgagroal: Management client done");
         pid_t p = (pid_t)payload_i;
         remove_client(p);
         break;
      case MANAGEMENT_SWITCH_TO:
         pgagroal_log_debug("pgagroal: Management switch to");
         int old_primary = -1;
         signed char server_state;
         for (int i = 0; old_primary == -1 && i < config->number_of_servers; i++)
         {
            server_state = atomic_load(&config->servers[i].state);
            if (server_state == SERVER_PRIMARY)
            {
               old_primary = i;
            }
         }

         if (!pgagroal_server_switch(payload_s))
         {
            if (!fork())
            {
               shutdown_ports();
               if (old_primary != -1)
               {
                  pgagroal_flush_server(old_primary);
               }
               else
               {
                  pgagroal_flush(FLUSH_GRACEFULLY, "*");
               }
            }
            pgagroal_prometheus_failed_servers();
         }
         break;
      case MANAGEMENT_RELOAD:
         pgagroal_log_debug("pgagroal: Management reload");
         reload_configuration();
         break;
      default:
         pgagroal_log_debug("pgagroal: Unknown management id: %d", id);
         break;
   }

   if (keep_running && config->gracefully)
   {
      if (atomic_load(&config->active_connections) == 0)
      {
         pgagroal_pool_status();
         keep_running = 0;
         ev_break(loop, EVBREAK_ALL);
      }
   }

   pgagroal_disconnect(client_fd);

   pgagroal_prometheus_self_sockets_sub();
}

static void
accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_metrics();

         free(metrics_fds);
         metrics_fds = NULL;
         metrics_fds_length = 0;

         if (pgagroal_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
         }
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   if (!fork())
   {
      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_prometheus(client_fd);
   }

   pgagroal_disconnect(client_fd);
   pgagroal_prometheus_self_sockets_sub();
}

static void
accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_management_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   memset(&address, 0, sizeof(address));

   config = (struct configuration*)shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_management();

         free(management_fds);
         management_fds = NULL;
         management_fds_length = 0;

         if (pgagroal_bind(config->host, config->management, &management_fds, &management_fds_length))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgagroal_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   if (!fork())
   {
      char* addr = malloc(strlen(address) + 1);
      memset(addr, 0, strlen(address) + 1);
      memcpy(addr, address, strlen(address));

      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_remote_management(client_fd, addr);
   }

   pgagroal_disconnect(client_fd);
   pgagroal_prometheus_self_sockets_sub();
}

static void
shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgagroal_log_debug("pgagroal: shutdown requested");
   pgagroal_pool_status();
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
reload_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgagroal_log_debug("pgagroal: reload requested");
   reload_configuration();
}

static void
graceful_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_log_debug("pgagroal: gracefully requested");

   pgagroal_pool_status();
   config->gracefully = true;

   if (atomic_load(&config->active_connections) == 0)
   {
      pgagroal_pool_status();
      keep_running = 0;
      ev_break(loop, EVBREAK_ALL);
   }
}

static void
coredump_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgagroal_log_info("pgagroal: core dump requested");
   pgagroal_pool_status();
   abort();
}

static void
idle_timeout_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("idle_timeout_cb: got invalid event: %s", strerror(errno));
      return;
   }

   /* pgagroal_idle_timeout() is always in a fork() */
   if (!fork())
   {
      shutdown_ports();
      pgagroal_idle_timeout();
   }
}

static void
validation_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("validation_cb: got invalid event: %s", strerror(errno));
      return;
   }

   /* pgagroal_validation() is always in a fork() */
   if (!fork())
   {
      shutdown_ports();
      pgagroal_validation();
   }
}

static void
disconnect_client_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("disconnect_client_cb: got invalid event: %s", strerror(errno));
      return;
   }

   /* main_pipeline.periodic is always in a fork() */
   if (!fork())
   {
      shutdown_ports();
      main_pipeline.periodic();
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
#ifdef HAVE_LINUX
      case ENONET:
#endif
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
         return false;
         break;
   }

   return true;
}

static void
add_client(pid_t pid)
{
   struct client* c = NULL;

   c = (struct client*)malloc(sizeof(struct client));
   c->pid = pid;
   c->next = NULL;

   if (clients == NULL)
   {
      clients = c;
   }
   else
   {
      struct client* last = NULL;

      last = clients;

      while (last->next != NULL)
      {
         last = last->next;
      }

      last->next = c;
   }
}

static void
remove_client(pid_t pid)
{
   struct client* c = NULL;
   struct client* p = NULL;

   c = clients;
   p = NULL;

   if (c != NULL)
   {
      while (c->pid != pid)
      {
         p = c;
         c = c->next;

         if (c == NULL)
         {
            return;
         }
      }

      if (c == clients)
      {
         clients = c->next;
      }
      else
      {
         p->next = c->next;
      }

      free(c);
   }
}

static void
reload_configuration(void)
{
   char pgsql[MISC_LENGTH];
   struct configuration* config;

   config = (struct configuration*)shmem;

   shutdown_io();
   shutdown_uds();
   shutdown_metrics();
   shutdown_management();

   pgagroal_reload_configuration();

   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->port);

   if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
      exit(1);
   }

   free(main_fds);
   main_fds = NULL;
   main_fds_length = 0;

   if (pgagroal_bind(config->host, config->port, &main_fds, &main_fds_length))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->port);
      exit(1);
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
      exit(1);
   }

   start_io();
   start_uds();

   if (config->metrics > 0)
   {
      free(metrics_fds);
      metrics_fds = NULL;
      metrics_fds_length = 0;

      /* Bind metrics socket */
      if (pgagroal_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->metrics);
         exit(1);
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
         exit(1);
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      free(management_fds);
      management_fds = NULL;
      management_fds_length = 0;

      /* Bind management socket */
      if (pgagroal_bind(config->host, config->management, &management_fds, &management_fds_length))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->host, config->management);
         exit(1);
      }

      if (management_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
         exit(1);
      }

      start_management();
   }

   for (int i = 0; i < main_fds_length; i++)
   {
      pgagroal_log_debug("Socket: %d", *(main_fds + i));
   }
   pgagroal_log_debug("Unix Domain Socket: %d", unix_pgsql_socket);
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgagroal_log_debug("Remote management: %d", *(management_fds + i));
   }
}

static int
create_pidfile(void)
{
   char buffer[64];
   pid_t pid;
   int r;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      // check pidfile is not there
      if (access(config->pidfile, F_OK) == 0)
      {
         pgagroal_log_fatal("PID file [%s] exists, is there another instance running ?", config->pidfile);
         goto error;
      }

      pid = getpid();

      fd = open(config->pidfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if (fd < 0)
      {
         printf("Could not create PID file '%s' due to %s\n", config->pidfile, strerror(errno));
         goto error;
      }

      snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

      r = write(fd, &buffer[0], strlen(buffer));
      if (r < 0)
      {
         printf("Could not write pidfile '%s' due to %s\n", config->pidfile, strerror(errno));
         goto error;
      }

      close(fd);
   }

   return 0;

error:

   return 1;
}

static void
remove_pidfile(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) > 0 && access(config->pidfile, F_OK) == 0)
   {
      unlink(config->pidfile);
   }
}

static void
shutdown_ports(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   shutdown_io();

   if (config->metrics > 0)
   {
      shutdown_metrics();
   }

   if (config->management > 0)
   {
      shutdown_management();
   }
}
