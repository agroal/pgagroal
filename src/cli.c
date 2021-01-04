/*
 * Copyright (C) 2021 Red Hat
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
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <openssl/ssl.h>

#define ACTION_UNKNOWN         0
#define ACTION_FLUSH           1
#define ACTION_GRACEFULLY      2
#define ACTION_STOP            3
#define ACTION_STATUS          4
#define ACTION_DETAILS         5
#define ACTION_ISALIVE         6
#define ACTION_CANCELSHUTDOWN  7
#define ACTION_ENABLEDB        8
#define ACTION_DISABLEDB       9
#define ACTION_RESET          10
#define ACTION_RESET_SERVER   11
#define ACTION_SWITCH_TO      12
#define ACTION_RELOAD         13

static int flush(SSL* ssl, int socket, int32_t mode);
static int enabledb(SSL* ssl, int socket, char* database);
static int disabledb(SSL* ssl, int socket, char* database);
static int gracefully(SSL* ssl, int socket);
static int stop(SSL* ssl, int socket);
static int cancel_shutdown(SSL* ssl, int socket);
static int status(SSL* ssl, int socket);
static int details(SSL* ssl, int socket);
static int isalive(SSL* ssl, int socket);
static int reset(SSL* ssl, int socket);
static int reset_server(SSL* ssl, int socket, char* server);
static int switch_to(SSL* ssl, int socket, char* server);
static int reload(SSL* ssl, int socket);

static void
version(void)
{
   printf("pgagroal-cli %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal-cli %s\n", VERSION);
   printf("  Command line utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file\n");
   printf("  -h, --host HOST          Set the host name\n");
   printf("  -p, --port PORT          Set the port number\n");
   printf("  -U, --user USERNAME      Set the user name\n");
   printf("  -P, --password PASSWORD  Set the password\n");
   printf("  -L, --logfile FILE       Set the log file\n");
   printf("  -v, --verbose            Output text string of result\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  flush-idle               Flush idle connections\n");
   printf("  flush-gracefully         Flush all connections gracefully\n");
   printf("  flush-all                Flush all connections. USE WITH CAUTION !\n");
   printf("  is-alive                 Is pgagroal alive\n");
   printf("  enable                   Enable a database\n");
   printf("  disable                  Disable a database\n");
   printf("  gracefully               Stop pgagroal gracefully\n");
   printf("  stop                     Stop pgagroal\n");
   printf("  cancel-shutdown          Cancel the graceful shutdown\n");
   printf("  status                   Status of pgagroal\n");
   printf("  details                  Detailed status of pgagroal\n");
   printf("  switch-to                Switch to another primary\n");
   printf("  reload                   Reload the configuration\n");
   printf("  reset                    Reset the Prometheus statistics\n");
   printf("  reset-server             Reset the state of a server\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char **argv)
{
   int socket = -1;
   SSL* s_ssl = NULL;
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   char* host = NULL;
   char* port = NULL;
   char* username = NULL;
   char* password = NULL;
   bool verbose = false;
   char* logfile = NULL;
   bool do_free = true;
   int c;
   int option_index = 0;
   size_t size;
   int32_t action = ACTION_UNKNOWN;
   int32_t mode = FLUSH_IDLE;
   char* database = NULL;
   char un[MAX_USERNAME_LENGTH];
   char* server = NULL;
   struct configuration* config;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config",  required_argument, 0, 'c'},
         {"host",  required_argument, 0, 'h'},
         {"port",  required_argument, 0, 'p'},
         {"user",  required_argument, 0, 'U'},
         {"password",  required_argument, 0, 'P'},
         {"logfile",  required_argument, 0, 'L'},
         {"verbose", no_argument, 0, 'v'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:",
                      long_options, &option_index);

      if (c == -1)
         break;

      switch (c)
      {
         case 'c':
            configuration_path = optarg;
            break;
         case 'h':
            host = optarg;
            break;
         case 'p':
            port = optarg;
            break;
         case 'U':
            username = optarg;
            break;
         case 'P':
            password = optarg;
            break;
         case 'L':
            logfile = optarg;
            break;
         case 'v':
            verbose = true;
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
      printf("pgagroal-cli: Using the root account is not allowed\n");
      exit(1);
   }

   if (configuration_path != NULL && (host != NULL || port != NULL))
   {
      printf("pgagroal-cli: Use either -c or -h/-p to define endpoint\n");
      exit(1);
   }

   size = sizeof(struct configuration);
   if (pgagroal_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      printf("pgagroal-cli: Error creating shared memory\n");
      exit(1);
   }
   pgagroal_init_configuration(shmem);

   if (configuration_path != NULL)
   {
      ret = pgagroal_read_configuration(shmem, configuration_path);
      if (ret)
      {
         printf("pgagroal-cli: Configuration not found: %s\n", configuration_path);
         exit(1);
      }

      if (logfile)
      {
         config = (struct configuration*)shmem;

         config->log_type = PGAGROAL_LOGGING_TYPE_FILE;
         memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }

      if (pgagroal_start_logging())
      {
         exit(1);
      }

      config = (struct configuration*)shmem;
   }
   else
   {
      ret = pgagroal_read_configuration(shmem, "/etc/pgagroal/pgagroal.conf");
      if (ret)
      {
         if (host == NULL || port == NULL)
         {
            printf("pgagroal-cli: Host and port must be specified\n");
            exit(1);
         }
      }
      else
      {
         configuration_path = "/etc/pgagroal/pgagroal.conf";

         if (logfile)
         {
            config = (struct configuration*)shmem;

            config->log_type = PGAGROAL_LOGGING_TYPE_FILE;
            memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
         }

         if (pgagroal_start_logging())
         {
            exit(1);
         }

         config = (struct configuration*)shmem;
      }
   }

   if (argc > 0)
   {
      if (!strcmp("flush-idle", argv[argc - 1]))
      {
         mode = FLUSH_IDLE;
         action = ACTION_FLUSH;
      }
      else if (!strcmp("flush-gracefully", argv[argc - 1]))
      {
         mode = FLUSH_GRACEFULLY;
         action = ACTION_FLUSH;
      }
      else if (!strcmp("flush-all", argv[argc - 1]))
      {
         mode = FLUSH_ALL;
         action = ACTION_FLUSH;
      }
      else if (!strcmp("enable", argv[argc - 1]) || !strcmp("enable", argv[argc - 2]))
      {
         action = ACTION_ENABLEDB;
         if (!strcmp("enable", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
      }
      else if (!strcmp("disable", argv[argc - 1]) || !strcmp("disable", argv[argc - 2]))
      {
         action = ACTION_DISABLEDB;
         if (!strcmp("disable", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
      }
      else if (!strcmp("gracefully", argv[argc - 1]))
      {
         action = ACTION_GRACEFULLY;
      }
      else if (!strcmp("stop", argv[argc - 1]))
      {
         action = ACTION_STOP;
      }
      else if (!strcmp("status", argv[argc - 1]))
      {
         action = ACTION_STATUS;
      }
      else if (!strcmp("details", argv[argc - 1]))
      {
         action = ACTION_DETAILS;
      }
      else if (!strcmp("is-alive", argv[argc - 1]))
      {
         action = ACTION_ISALIVE;
      }
      else if (!strcmp("cancel-shutdown", argv[argc - 1]))
      {
         action = ACTION_CANCELSHUTDOWN;
      }
      else if (!strcmp("reset", argv[argc - 1]))
      {
         action = ACTION_RESET;
      }
      else if (!strcmp("reset-server", argv[argc - 1]) || !strcmp("reset-server", argv[argc - 2]))
      {
         if (!strcmp("reset-server", argv[argc - 2]))
         {
            action = ACTION_RESET_SERVER;
            server = argv[argc - 1];
         }
      }
      else if (!strcmp("switch-to", argv[argc - 1]) || !strcmp("switch-to", argv[argc - 2]))
      {
         if (!strcmp("switch-to", argv[argc - 2]))
         {
            action = ACTION_SWITCH_TO;
            server = argv[argc - 1];
         }
      }
      else if (!strcmp("reload", argv[argc - 1]))
      {
         /* Local connection only */
         if (configuration_path != NULL)
         {
            action = ACTION_RELOAD;
         }
      }

      if (action != ACTION_UNKNOWN)
      {
         if (configuration_path != NULL)
         {
            /* Local connection */
            if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
            {
               exit_code = 1;
               goto done;
            }
         }
         else
         {
            /* Remote connection */
            if (pgagroal_connect(host, atoi(port), &socket))
            {
               printf("pgagroal-cli: No route to host: %s:%s\n", host, port);
               goto done;
            }

            /* User name */
            if (username == NULL)
            {
username:
               printf("User name: ");

               memset(&un, 0, sizeof(un));
               fgets(&un[0], sizeof(un), stdin);
               un[strlen(un) - 1] = 0;
               username = &un[0];
            }

            if (username == NULL || strlen(username) == 0)
            {
               goto username;
            }

            /* Password */
            if (password == NULL)
            {
password:
               if (password != NULL)
               {
                  free(password);
                  password = NULL;
               }

               printf("Password : ");
               password = pgagroal_get_password();
               printf("\n");
            }
            else
            {
               do_free = false;
            }

            for (int i = 0; i < strlen(password); i++)
            {
               if ((unsigned char)(*(password + i)) & 0x80)
               {
                  goto password;
               }
            }

            /* Authenticate */
            if (pgagroal_remote_management_scram_sha256(username, password, socket, &s_ssl) != AUTH_SUCCESS)
            {
               printf("pgagroal-cli: Bad credentials for %s\n", username);
               goto done;
            }
         }
      }

      if (action == ACTION_FLUSH)
      {
         exit_code = flush(s_ssl, socket, mode);
      }
      else if (action == ACTION_ENABLEDB)
      {
         exit_code = enabledb(s_ssl, socket, database);
      }
      else if (action == ACTION_DISABLEDB)
      {
         exit_code = disabledb(s_ssl, socket, database);
      }
      else if (action == ACTION_GRACEFULLY)
      {
         exit_code = gracefully(s_ssl, socket);
      }
      else if (action == ACTION_STOP)
      {
         exit_code = stop(s_ssl, socket);
      }
      else if (action == ACTION_CANCELSHUTDOWN)
      {
         exit_code = cancel_shutdown(s_ssl, socket);
      }
      else if (action == ACTION_STATUS)
      {
         exit_code = status(s_ssl, socket);
      }
      else if (action == ACTION_DETAILS)
      {
         exit_code = details(s_ssl, socket);
      }
      else if (action == ACTION_ISALIVE)
      {
         exit_code = isalive(s_ssl, socket);
      }
      else if (action == ACTION_RESET)
      {
         exit_code = reset(s_ssl, socket);
      }
      else if (action == ACTION_RESET_SERVER)
      {
         exit_code = reset_server(s_ssl, socket, server);
      }
      else if (action == ACTION_SWITCH_TO)
      {
         exit_code = switch_to(s_ssl, socket, server);
      }
      else if (action == ACTION_RELOAD)
      {
         exit_code = reload(s_ssl, socket);
      }
   }

done:

   if (s_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(s_ssl);
      res = SSL_shutdown(s_ssl);
      if (res == 0)
      {
         SSL_shutdown(s_ssl);
      }
      SSL_free(s_ssl);
      SSL_CTX_free(ctx);
   }

   pgagroal_disconnect(socket);

   if (action == ACTION_UNKNOWN)
   {
      usage();
      exit_code = 1;
   }
   
   if (configuration_path != NULL)
   {
      if (action != ACTION_UNKNOWN && exit_code != 0)
      {
         printf("No connection to pgagroal on %s\n", config->unix_socket_dir);
      }
   }

   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(shmem, size);

   if (do_free)
   {
      free(password);
   }

   if (verbose)
   {
      if (exit_code == 0)
      {
         printf("Success (0)\n");
      }
      else
      {
         printf("Error (%d)\n", exit_code);
      }
   }

   return exit_code;
}

static int
flush(SSL* ssl, int socket, int32_t mode)
{
   if (pgagroal_management_flush(ssl, socket, mode))
   {
      return 1;
   }

   return 0;
}

static int
enabledb(SSL* ssl, int socket, char* database)
{
   if (pgagroal_management_enabledb(ssl, socket, database))
   {
      return 1;
   }

   return 0;
}

static int
disabledb(SSL* ssl, int socket, char* database)
{
   if (pgagroal_management_disabledb(ssl, socket, database))
   {
      return 1;
   }

   return 0;
}

static int
gracefully(SSL* ssl, int socket)
{
   if (pgagroal_management_gracefully(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
stop(SSL* ssl, int socket)
{
   if (pgagroal_management_stop(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
cancel_shutdown(SSL* ssl, int socket)
{
   if (pgagroal_management_cancel_shutdown(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
status(SSL* ssl, int socket)
{
   if (pgagroal_management_status(ssl, socket) == 0)
   {
      pgagroal_management_read_status(ssl, socket);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
details(SSL* ssl, int socket)
{
   if (pgagroal_management_details(ssl, socket) == 0)
   {
      pgagroal_management_read_status(ssl, socket);
      pgagroal_management_read_details(ssl, socket);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
isalive(SSL* ssl, int socket)
{
   int status = -1;

   if (pgagroal_management_isalive(ssl, socket) == 0)
   {
      if (pgagroal_management_read_isalive(ssl, socket, &status))
      {
         return 1;
      }

      if (status != 1 && status != 2)
      {
         return 1;
      }
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
reset(SSL* ssl, int socket)
{
   if (pgagroal_management_reset(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
reset_server(SSL* ssl, int socket, char* server)
{
   if (pgagroal_management_reset_server(ssl, socket, server))
   {
      return 1;
   }

   return 0;
}

static int
switch_to(SSL* ssl, int socket, char* server)
{
   if (pgagroal_management_switch_to(ssl, socket, server))
   {
      return 1;
   }

   return 0;
}

static int
reload(SSL* ssl, int socket)
{
   if (pgagroal_management_reload(ssl, socket))
   {
      return 1;
   }

   return 0;
}
