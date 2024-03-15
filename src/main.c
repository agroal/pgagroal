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

/* pgagroal */
#include <pgagroal.h>
#include <configuration.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
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
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

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
static void max_connection_age_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void rotate_frontend_password_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void validation_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void disconnect_client_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static bool accept_fatal(int error);
static void add_client(pid_t pid);
static void remove_client(pid_t pid);
static void reload_configuration(void);
static void create_pidfile_or_exit(void);
static void remove_pidfile(void);
static void shutdown_ports(void);

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
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

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
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

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
   printf("pgagroal %s\n", PGAGROAL_VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal %s\n", PGAGROAL_VERSION);
   printf("  High-performance connection pool for PostgreSQL\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal [ -c CONFIG_FILE ] [ -a HBA_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE           Set the path to the pgagroal.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_CONF_FILE);
   printf("  -a, --hba HBA_FILE                 Set the path to the pgagroal_hba.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_HBA_FILE);
   printf("  -l, --limit LIMIT_FILE             Set the path to the pgagroal_databases.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_LIMIT_FILE);
   printf("  -u, --users USERS_FILE             Set the path to the pgagroal_users.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_USERS_FILE);
   printf("  -F, --frontend FRONTEND_USERS_FILE Set the path to the pgagroal_frontend_users.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_FRONTEND_USERS_FILE);
   printf("  -A, --admins ADMINS_FILE           Set the path to the pgagroal_admins.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_ADMINS_FILE);
   printf("  -S, --superuser SUPERUSER_FILE     Set the path to the pgagroal_superuser.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_SUPERUSER_FILE);
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
   struct ev_periodic max_connection_age;
   struct ev_periodic validation;
   struct ev_periodic disconnect_client;
   struct ev_periodic rotate_frontend_password;
   struct rlimit flimit;
   size_t shmem_size;
   size_t pipeline_shmem_size = 0;
   size_t prometheus_shmem_size = 0;
   size_t prometheus_cache_shmem_size = 0;
   size_t tmp_size;
   struct main_configuration* config = NULL;
   int ret;
   int c;
   bool conf_file_mandatory;
   char message[MISC_LENGTH]; // a generic message used for errors
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
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      errx(1, "Using the root account is not allowed");
   }

   shmem_size = sizeof(struct main_configuration);
   if (pgagroal_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      errx(1, "Error in creating shared memory");
   }

   pgagroal_init_configuration(shmem);
   config = (struct main_configuration*)shmem;

   memset(&known_fds, 0, sizeof(known_fds));
   memset(message, 0, MISC_LENGTH);

   // the main configuration file is mandatory!
   configuration_path = configuration_path != NULL ? configuration_path : PGAGROAL_DEFAULT_CONF_FILE;
   if ((ret = pgagroal_read_configuration(shmem, configuration_path, true)) != PGAGROAL_CONFIGURATION_STATUS_OK)
   {
      // the configuration has some problem, build up a descriptive message
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {
         snprintf(message, MISC_LENGTH, "Configuration file not found");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "Too many sections");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
         snprintf(message, MISC_LENGTH, "Invalid configuration file");
      }
      else if (ret > 0)
      {
         snprintf(message, MISC_LENGTH, "%d problematic or duplicated section%c",
                  ret,
                  ret > 1 ? 's' : ' ');
      }

#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=%s: %s", message, configuration_path);
#endif
      errx(1, "%s (file <%s>)", message, configuration_path);
   }

   memcpy(&config->common.configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   // the HBA file is mandatory!
   hba_path = hba_path != NULL ? hba_path : PGAGROAL_DEFAULT_HBA_FILE;
   memset(message, 0, MISC_LENGTH);
   ret = pgagroal_read_hba_configuration(shmem, hba_path);
   if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
   {
      snprintf(message, MISC_LENGTH, "HBA configuration file not found");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=%s: %s", message, hba_path);
#endif
      errx(1, "%s (file <%s>)", message, hba_path);
   }
   else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
   {
      snprintf(message, MISC_LENGTH, "HBA too many entries (max %d)", NUMBER_OF_HBAS);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=%s: %s", message, hba_path);
#endif

      errx(1, "%s (file <%s>)", message, hba_path);
   }

   memcpy(&config->hba_path[0], hba_path, MIN(strlen(hba_path), MAX_PATH - 1));

   conf_file_mandatory = true;
read_limit_path:
   if (limit_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_read_limit_configuration(shmem, limit_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->limit_path[0], limit_path, MIN(strlen(limit_path), MAX_PATH - 1));
      }
      else if (conf_file_mandatory && ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {

         snprintf(message, MISC_LENGTH, "LIMIT configuration file not found");
         printf("pgagroal: %s (file <%s>)\n", message, limit_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, limit_path);
#endif
         exit(1);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {

         snprintf(message, MISC_LENGTH, "Too many limit entries");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, limit_path);
#endif
         errx(1, "%s (file <%s>)", message, limit_path);
      }

   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      limit_path = PGAGROAL_DEFAULT_LIMIT_FILE;
      conf_file_mandatory = false;
      goto read_limit_path;
   }

   conf_file_mandatory = true;
read_users_path:
   if (users_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_read_users_configuration(shmem, users_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {

         snprintf(message, MISC_LENGTH, "USERS configuration file not found");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s : %s", message, users_path);
#endif
         errx(1, "%s  (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO
               || ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT)
      {

         snprintf(message, MISC_LENGTH, "Invalid master key file");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, users_path);
#endif
         errx(1, "%s (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {

         snprintf(message, MISC_LENGTH, "USERS: too many users defined (%d, max %d)", config->number_of_users, NUMBER_OF_USERS);

#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, users_path);
#endif
         errx(1, "%s (file <%s>)", message, users_path);
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      users_path = PGAGROAL_DEFAULT_USERS_FILE;
      conf_file_mandatory = false;
      goto read_users_path;
   }

   conf_file_mandatory = true;
read_frontend_users_path:
   if (frontend_users_path != NULL)
   {
      ret = pgagroal_read_frontend_users_configuration(shmem, frontend_users_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         memset(message, 0, MISC_LENGTH);
         snprintf(message, MISC_LENGTH, "FRONTEND USERS configuration file not found");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, frontend_users_path);
#endif
         errx(1, "%s (file <%s>)", message, frontend_users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT
               || ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         errx(1, "Invalid master key file");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         memset(message, 0, MISC_LENGTH);
         snprintf(message, MISC_LENGTH, "FRONTEND USERS: Too many users defined %d (max %d)",
                  config->number_of_frontend_users, NUMBER_OF_USERS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, frontend_users_path);
#endif
         errx(1, "%s (file <%s>)", message, frontend_users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->frontend_users_path[0], frontend_users_path, MIN(strlen(frontend_users_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      frontend_users_path = PGAGROAL_DEFAULT_FRONTEND_USERS_FILE;
      conf_file_mandatory = false;
      goto read_frontend_users_path;
   }

   conf_file_mandatory = true;
read_admins_path:
   if (admins_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_read_admins_configuration(shmem, admins_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {

         snprintf(message, MISC_LENGTH, "ADMINS configuration file not found");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, admins_path);
#endif
         errx(1, "%s (file <%s>)", message, admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT
               || ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         errx(1, "Invalid master key file");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s %s", message, admins_path);
#endif
         errx(1, "%s (file <%s>)", message, admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      admins_path = PGAGROAL_DEFAULT_ADMINS_FILE;
      conf_file_mandatory = false;
      goto read_admins_path;
   }

   conf_file_mandatory = true;
read_superuser_path:
   if (superuser_path != NULL)
   {
      ret = pgagroal_read_superuser_configuration(shmem, superuser_path);
      memset(message, 0, MISC_LENGTH);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         snprintf(message, MISC_LENGTH, "SUPERUSER configuration file not found");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, superuser_path);
#endif
         errx(1, "%s (file <%s>)", message, superuser_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT || ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         errx(1, "Invalid master key file");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "SUPERUSER: Too many superusers defined (max 1)");
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=%s: %s", message, superuser_path);
#endif
         errx(1, "%s (file <%s>)", message, superuser_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->superuser_path[0], superuser_path, MIN(strlen(superuser_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      superuser_path = PGAGROAL_DEFAULT_SUPERUSER_FILE;
      conf_file_mandatory = false;
      goto read_superuser_path;
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
      errx(1, "Failed to start logging");
   }

   if (pgagroal_validate_configuration(shmem, has_unix_socket, has_main_sockets))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      errx(1, "Invalid configuration");
   }
   if (pgagroal_validate_hba_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid HBA configuration");
#endif
      errx(1, "Invalid HBA configuration");
   }
   if (pgagroal_validate_limit_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid LIMIT configuration");
#endif
      errx(1, "Invalid LIMIT configuration");
   }
   if (pgagroal_validate_users_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      errx(1, "Invalid USERS configuration");
   }
   if (pgagroal_validate_frontend_users_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid FRONTEND USERS configuration");
#endif
      errx(1, "Invalid FRONTEND USERS configuration");
   }
   if (pgagroal_validate_admins_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      errx(1, "Invalid ADMINS configuration");
   }

   if (pgagroal_resize_shared_memory(shmem_size, shmem, &tmp_size, &tmp_shmem))
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      errx(1, "Error in creating shared memory");
   }
   if (pgagroal_destroy_shared_memory(shmem, shmem_size) == -1)
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in destroying shared memory");
#endif
      errx(1, "Error in destroying shared memory");
   }
   shmem_size = tmp_size;
   shmem = tmp_shmem;
   config = (struct main_configuration*)shmem;

   if (pgagroal_init_prometheus(&prometheus_shmem_size, &prometheus_shmem))
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus shared memory");
#endif
      errx(1, "Error in creating and initializing prometheus shared memory");
   }

   if (pgagroal_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
      errx(1, "Error in creating and initializing prometheus cache shared memory");
   }

   if (getrlimit(RLIMIT_NOFILE, &flimit) == -1)
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Unable to find limit due to %s", strerror(errno));
#endif
      err(1, "Unable to find limit");
   }

   /* We are "reserving" 30 file descriptors for pgagroal main */
   if (config->max_connections > (flimit.rlim_cur - 30))
   {
#ifdef HAVE_LINUX
      sd_notifyf(0,
                 "STATUS=max_connections is larger than the file descriptor limit (%ld available)",
                 (long)(flimit.rlim_cur - 30));
#endif
      errx(1, "max_connections is larger than the file descriptor limit (%ld available)", (long)(flimit.rlim_cur - 30));
   }

   if (daemon)
   {
      if (config->common.log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
      {
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         errx(1, "Daemon mode can't be used with console logging");
      }

      pid = fork();

      if (pid < 0)
      {
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode failed");
#endif
         errx(1, "Daemon mode failed");
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

   create_pidfile_or_exit();

   pgagroal_pool_init();
   pgagroal_initialize_random();

   pgagroal_set_proc_title(argc, argv, "main", NULL);

   /* Bind Unix Domain Socket for file descriptor transfers */
   if (pgagroal_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#endif
      goto error;
   }

   if (!has_unix_socket)
   {
      char pgsql[MISC_LENGTH];

      memset(&pgsql, 0, sizeof(pgsql));
      snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

      if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
#endif
         goto error;
      }
   }

   /* Bind main socket */
   if (!has_main_sockets)
   {
      if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->common.port);
#endif
         goto error;
      }
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Too many descriptors %d", main_fds_length);
#endif
      goto error;
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
      goto error;
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
         goto error;
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
         goto error;
      }

      main_pipeline = transaction_pipeline();
   }
   else
   {
      pgagroal_log_fatal("pgagroal: Unknown pipeline identifier (%d)", config->pipeline);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Unknown pipeline identifier (%d)", config->pipeline);
#endif
      goto error;
   }

   if (main_pipeline.initialize(shmem, &pipeline_shmem, &pipeline_shmem_size))
   {
      pgagroal_log_fatal("pgagroal: Pipeline initialize error (%d)", config->pipeline);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Pipeline initialize error (%d)", config->pipeline);
#endif
      goto error;
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

   if (config->max_connection_age > 0)
   {
      ev_periodic_init (&max_connection_age, max_connection_age_cb, 0.,
                        MAX(1. * config->max_connection_age / 2., 5.), 0);
      ev_periodic_start (main_loop, &max_connection_age);
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

   if (config->rotate_frontend_password_timeout > 0)
   {
      ev_periodic_init (&rotate_frontend_password, rotate_frontend_password_cb, 0.,
                        config->rotate_frontend_password_timeout, 0);
      ev_periodic_start (main_loop, &rotate_frontend_password);
   }

   if (config->metrics > 0)
   {
      /* Bind metrics socket */
      if (pgagroal_bind(config->common.host, config->metrics, &metrics_fds, &metrics_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->metrics);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->metrics);
#endif
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         goto error;
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->management);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->management);
#endif
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         goto error;
      }

      start_management();
   }

   pgagroal_log_info("pgagroal: %s started on %s:%d",
                     PGAGROAL_VERSION,
                     config->common.host,
                     config->common.port);
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
         pgagroal_prefill_if_can(false, true);
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

error:
   remove_pidfile();
   exit(1);
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
   struct main_configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_main_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   ai = (struct accept_io*)watcher;
   config = (struct main_configuration*)shmem;

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
         snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
            exit(1);
         }

         free(main_fds);
         main_fds = NULL;
         main_fds_length = 0;

         if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
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
      char* addr = calloc(1, strlen(address) + 1);
      if (addr == NULL)
      {
         pgagroal_log_fatal("Cannot allocate memory for client address");
         return;
      }
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
   int payload_i, secondary_payload_i;
   char* payload_s = NULL;
   char* secondary_payload_s = NULL;
   struct main_configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct main_configuration*)shmem;

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
      case MANAGEMENT_CONFIG_LS:
         pgagroal_log_debug("pgagroal: Management conf ls");
         pgagroal_management_write_conf_ls(client_fd);
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
      case MANAGEMENT_CONFIG_GET:
         pgagroal_log_debug("pgagroal: Management config-get for key <%s>", payload_s);
         pgagroal_management_write_config_get(client_fd, payload_s);
         break;
      case MANAGEMENT_CONFIG_SET:
         // this command has a secondary payload to extract, that is the configuration value
         pgagroal_management_read_payload(client_fd, id, &secondary_payload_i, &secondary_payload_s);
         pgagroal_log_debug("pgagroal: Management config-set for key <%s> setting value to <%s>", payload_s, secondary_payload_s);
         pgagroal_management_write_config_set(client_fd, payload_s, secondary_payload_s);
         break;
      case MANAGEMENT_GET_PASSWORD:
      {
         // get frontend password
         char frontend_password[MAX_PASSWORD_LENGTH];
         memset(frontend_password, 0, sizeof(frontend_password));

         for (int i = 0; i < config->number_of_frontend_users; i++)
         {
            if (!strcmp(&config->frontend_users[i].username[0], payload_s))
            {
               memcpy(frontend_password, config->frontend_users[i].password, strlen(config->frontend_users[i].password));
            }
         }

         // Send password to the vault
         pgagroal_management_write_get_password(client_fd, frontend_password);
         pgagroal_disconnect(client_fd);
         return;
      }
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
   struct main_configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct main_configuration*)shmem;

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

         if (pgagroal_bind(config->common.host, config->metrics, &metrics_fds, &metrics_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->metrics);
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
   struct main_configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_management_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   memset(&address, 0, sizeof(address));

   config = (struct main_configuration*)shmem;

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

         if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->management);
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
      char* addr = calloc(1, strlen(address) + 1);
      if (addr == NULL)
      {
         pgagroal_log_fatal("Couldn't allocate address");
         return;
      }
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
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

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
max_connection_age_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("max_connection_age_cb: got invalid event: %s", strerror(errno));
      return;
   }

   /* max_connection_age() is always in a fork() */
   if (!fork())
   {
      shutdown_ports();
      pgagroal_max_connection_age();
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

static void
rotate_frontend_password_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   char* pwd;

   if (EV_ERROR & revents)
   {
      pgagroal_log_trace("rotate_frontend_password_cb: got invalid event: %s", strerror(errno));
      return;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->number_of_frontend_users; i++)
   {
      if (pgagroal_generate_password(config->rotate_frontend_password_length, &pwd))
      {
         pgagroal_log_debug("rotate_frontend_password_cb: unable to rotate password");
         return;
      }
      memcpy(&config->frontend_users[i].password, pwd, strlen(pwd) + 1);
      pgagroal_log_trace("rotate_frontend_password_cb: current pass for username=%s:%s", config->frontend_users[i].username, config->frontend_users[i].password);
      free(pwd);
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
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   shutdown_io();
   shutdown_uds();
   shutdown_metrics();
   shutdown_management();

   pgagroal_reload_configuration();

   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

   if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
      goto error;
   }

   free(main_fds);
   main_fds = NULL;
   main_fds_length = 0;

   if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
      goto error;
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
      goto error;
   }

   start_io();
   start_uds();

   if (config->metrics > 0)
   {
      free(metrics_fds);
      metrics_fds = NULL;
      metrics_fds_length = 0;

      /* Bind metrics socket */
      if (pgagroal_bind(config->common.host, config->metrics, &metrics_fds, &metrics_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->metrics);
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
         goto error;
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      free(management_fds);
      management_fds = NULL;
      management_fds_length = 0;

      /* Bind management socket */
      if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->non_blocking, &config->buffer_size, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->management);
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
         goto error;
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

   return;

error:
   remove_pidfile();
   exit(1);
}

/**
 * Creates the pid file for the running pooler.
 * If a pid file already exists, or if the file cannot be written,
 * the function kills (exits) the current process.
 *
 */
static void
create_pidfile_or_exit(void)
{
   char buffer[64];
   pid_t pid;
   int r;
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      pid = getpid();

      fd = open(config->pidfile, O_WRONLY | O_CREAT | O_EXCL, 0640);
      if (errno == EEXIST)
      {
         errx(1, "PID file <%s> exists, is there another instance running ?", config->pidfile);
      }
      else if (errno == EACCES)
      {
         errx(1, "PID file <%s> cannot be created due to lack of permissions", config->pidfile);
      }
      else if (fd < 0)
      {
         err(1, "Could not create PID file <%s>", config->pidfile);
      }

      snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

      r = write(fd, &buffer[0], strlen(buffer));
      if (r < 0)
      {
         errx(1, "Could not write into PID file <%s>", config->pidfile);
      }

      close(fd);
   }
}

static void
remove_pidfile(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      if (unlink(config->pidfile))
      {
         warn("Cannot remove PID file <%s>", config->pidfile);
      }
   }
}

static void
shutdown_ports(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

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
