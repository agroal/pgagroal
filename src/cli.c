/*
 * Copyright (C) 2023 Red Hat
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
#include <err.h>
#include <errno.h>

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
#define ACTION_CONFIG_GET     14
#define ACTION_CONFIG_SET     15

static int flush(SSL* ssl, int socket, int32_t mode, char* database);
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
static int config_get(SSL* ssl, int socket, char* config_key, bool verbose);
static int config_set(SSL* ssl, int socket, char* config_key, char* config_value, bool verbose);

static void
version(void)
{
   printf("pgagroal-cli %s\n", PGAGROAL_VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal-cli %s\n", PGAGROAL_VERSION);
   printf("  Command line utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file\n");
   printf("                           Default: %s\n", PGAGROAL_DEFAULT_CONF_FILE);
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
   printf("  config-get               Retrieves a configuration value\n");
   printf("  config-set               Modifies a configuration value\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char** argv)
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
   struct configuration* config = NULL;
   bool remote_connection = false;
   long l_port;
   char* config_key = NULL; /* key for a configuration setting */
   char* config_value = NULL; /* value for a configuration setting */

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"host", required_argument, 0, 'h'},
         {"port", required_argument, 0, 'p'},
         {"user", required_argument, 0, 'U'},
         {"password", required_argument, 0, 'P'},
         {"logfile", required_argument, 0, 'L'},
         {"verbose", no_argument, 0, 'v'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

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
      errx(1, "Using the root account is not allowed");
   }

   // if the user has specified the host and port
   // options, she wants a remote connection
   // but both remote connection parameters have to be set
   if (host != NULL || port != NULL)
   {
      remote_connection = host != NULL && port != NULL;
      if (!remote_connection)
      {
         printf("pgagroal-cli: you need both -h and -p options to perform a remote connection\n");
         exit(1);
      }
   }

   // if the user has specified either a username or a password
   // there must be all the other pieces for a remote connection
   if ((username != NULL || password != NULL) && !remote_connection)
   {
      errx(1, "you need also -h and -p options to perform a remote connection");
   }

   // and she cannot use "local" and "remote" connections at the same time
   if (configuration_path != NULL && remote_connection)
   {
      errx(1, "Use either -c or -h/-p to define endpoint");
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   size = sizeof(struct configuration);
   if (pgagroal_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      errx(1, "Error creating shared memory");
   }
   pgagroal_init_configuration(shmem);

   if (configuration_path != NULL)
   {
      ret = pgagroal_read_configuration(shmem, configuration_path, false);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {
         errx(1, "Configuration not found: <%s>", configuration_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         errx(1, "Too many sections in the configuration file <%s>", configuration_path);
      }

      if (logfile)
      {
         config = (struct configuration*)shmem;

         config->log_type = PGAGROAL_LOGGING_TYPE_FILE;
         memset(&config->log_path[0], 0, MISC_LENGTH);
         memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }

      if (pgagroal_start_logging())
      {
         errx(1, "Cannot start the logging subsystem");
      }

      config = (struct configuration*)shmem;
   }
   else
   {
      ret = pgagroal_read_configuration(shmem, PGAGROAL_DEFAULT_CONF_FILE, false);
      if (ret != PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         if (!remote_connection)
         {
            errx(1, "Host (-h) and port (-p) must be specified to connect to the remote host");
         }
      }
      else
      {
         configuration_path = PGAGROAL_DEFAULT_CONF_FILE;

         if (logfile)
         {
            config = (struct configuration*)shmem;

            config->log_type = PGAGROAL_LOGGING_TYPE_FILE;
            memset(&config->log_path[0], 0, MISC_LENGTH);
            memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
         }

         if (pgagroal_start_logging())
         {
            errx(1, "Cannot start the logging subsystem");
         }

         config = (struct configuration*)shmem;
      }
   }

   if (argc > 0)
   {
      if (!strcmp("flush-idle", argv[argc - 1]) || !strcmp("flush-idle", argv[argc - 2]))
      {
         mode = FLUSH_IDLE;
         action = ACTION_FLUSH;
         if (!strcmp("flush-idle", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
      }
      else if (!strcmp("flush-gracefully", argv[argc - 1]) || !strcmp("flush-gracefully", argv[argc - 2]))
      {
         mode = FLUSH_GRACEFULLY;
         action = ACTION_FLUSH;
         if (!strcmp("flush-gracefully", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
      }
      else if (!strcmp("flush-all", argv[argc - 1]) || !strcmp("flush-all", argv[argc - 2]))
      {
         mode = FLUSH_ALL;
         action = ACTION_FLUSH;
         if (!strcmp("flush-all", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
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
      else if (argc > 2 && !strncmp("config-get", argv[argc - 2], MISC_LENGTH) && strlen(argv[argc - 1]) > 0)
      {
         /* get a configuration value */
         action = ACTION_CONFIG_GET;
         config_key = argv[argc - 1];
      }
      else if (argc > 3 && !strncmp("config-set", argv[argc - 3], MISC_LENGTH)
               && strlen(argv[argc - 2]) > 0
               && strlen(argv[argc - 1]) > 0)
      {
         /* set a configuration value */
         action = ACTION_CONFIG_SET;
         config_key = argv[argc - 2];
         config_value = argv[argc - 1];
      }

      if (action != ACTION_UNKNOWN)
      {
         if (!remote_connection)
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

            l_port = strtol(port, NULL, 10);
            if ((errno == ERANGE && (l_port == LONG_MAX || l_port == LONG_MIN)) || (errno != 0 && l_port == 0))
            {
               warnx("Specified port %s out of range", port);
               goto done;
            }

            // cannot connect to port less than 1024 because pgagroal
            // cannot be run as root!
            if (l_port <= 1024)
            {
               warnx("Not allowed port %ld", l_port);
               goto done;
            }

            if (pgagroal_connect(host, (int)l_port, &socket))
            {
               warnx("No route to host: %s:%ld\n", host, l_port);
               goto done;
            }

            /* User name */
            if (username == NULL)
            {
username:
               printf("User name: ");

               memset(&un, 0, sizeof(un));
               if (fgets(&un[0], sizeof(un), stdin) == NULL)
               {
                  exit_code = 1;
                  goto done;
               }
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
               warnx("Bad credentials for %s\n", username);
               goto done;
            }
         }
      }

      if (action == ACTION_FLUSH)
      {
         exit_code = flush(s_ssl, socket, mode, database);
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
      else if (action == ACTION_CONFIG_GET)
      {
         exit_code = config_get(s_ssl, socket, config_key, verbose);
      }
      else if (action == ACTION_CONFIG_SET)
      {
         exit_code = config_set(s_ssl, socket, config_key, config_value, verbose);
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
      if (action != ACTION_UNKNOWN)
      {
         switch (exit_code)
         {
            case EXIT_STATUS_CONNECTION_ERROR:
               printf("Connection error on %s\n", config->unix_socket_dir);
               break;
            case EXIT_STATUS_DATA_ERROR:
            case EXIT_STATUS_OK:
               break;
         }

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
      warnx("%s (%d)", exit_code == EXIT_STATUS_OK ? "Success" : "Error", exit_code);
   }

   return exit_code;
}

static int
flush(SSL* ssl, int socket, int32_t mode, char* database)
{
   if (pgagroal_management_flush(ssl, socket, mode, database))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
enabledb(SSL* ssl, int socket, char* database)
{
   if (pgagroal_management_enabledb(ssl, socket, database))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
disabledb(SSL* ssl, int socket, char* database)
{
   if (pgagroal_management_disabledb(ssl, socket, database))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
gracefully(SSL* ssl, int socket)
{
   if (pgagroal_management_gracefully(ssl, socket))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
stop(SSL* ssl, int socket)
{
   if (pgagroal_management_stop(ssl, socket))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
cancel_shutdown(SSL* ssl, int socket)
{
   if (pgagroal_management_cancel_shutdown(ssl, socket))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
status(SSL* ssl, int socket)
{
   if (pgagroal_management_status(ssl, socket) == 0)
   {
      return pgagroal_management_read_status(ssl, socket);
   }
   else
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }
}

static int
details(SSL* ssl, int socket)
{
   if (pgagroal_management_details(ssl, socket) == 0)
   {
      if (pgagroal_management_read_status(ssl, socket) == 0)
      {
         return pgagroal_management_read_details(ssl, socket);
      }
   }

   // if here, an error occurred
   return EXIT_STATUS_CONNECTION_ERROR;

}

static int
isalive(SSL* ssl, int socket)
{
   int status = -1;

   if (pgagroal_management_isalive(ssl, socket) == 0)
   {
      if (pgagroal_management_read_isalive(ssl, socket, &status))
      {
         return EXIT_STATUS_CONNECTION_ERROR;
      }

      if (status != 1 && status != 2)
      {
         return EXIT_STATUS_CONNECTION_ERROR;
      }
   }
   else
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
reset(SSL* ssl, int socket)
{
   if (pgagroal_management_reset(ssl, socket))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
reset_server(SSL* ssl, int socket, char* server)
{
   if (pgagroal_management_reset_server(ssl, socket, server))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
switch_to(SSL* ssl, int socket, char* server)
{
   if (pgagroal_management_switch_to(ssl, socket, server))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

static int
reload(SSL* ssl, int socket)
{
   if (pgagroal_management_reload(ssl, socket))
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }

   return EXIT_STATUS_OK;
}

/**
 * Entry point for a config-get command line action.
 *
 * First it sends the message to pgagroal process to execute a config-get,
 * then reads back the answer.
 *
 * @param ssl the SSL mode
 * @param socket the socket file descriptor
 * @param config_key the key of the configuration parameter, that is the name
 * of the configuration parameter to read.
 * @param verbose if true the function will print on STDOUT also the config key
 * @returns 0 on success, 1 on network failure, 2 on data failure
 */
static int
config_get(SSL* ssl, int socket, char* config_key, bool verbose)
{
   char* buffer = NULL;

   if (!config_key || strlen(config_key) > MISC_LENGTH)
   {
      goto error;
   }

   if (pgagroal_management_config_get(ssl, socket, config_key))
   {
      goto error;
   }
   else
   {
      buffer = calloc(1, MISC_LENGTH);
      if (buffer == NULL)
      {
         goto error;
      }
      if (pgagroal_management_read_config_get(socket, &buffer))
      {
         free(buffer);
         goto error;
      }

      // an empty response means that the
      // requested configuration parameter has not been
      // found, so throw an error
      if (buffer && strlen(buffer))
      {
         if (verbose)
         {
            printf("%s = %s\n", config_key, buffer);
         }
         else
         {
            printf("%s\n", buffer);
         }
      }
      else
      {
         free(buffer);
         return EXIT_STATUS_DATA_ERROR;
      }

      free(buffer);
   }

   return EXIT_STATUS_OK;

error:
   return EXIT_STATUS_CONNECTION_ERROR;
}

/**
 * Entry point for a config-set command.
 *
 * The function requires the configuration parameter to set and its value.
 * It then sends the command over the socket and reads the answer back.
 *
 * @param ssl the SSL connection
 * @param socket the socket to use
 * @param config_key the parameter name to set
 * @param config_value the value to set the parameter to
 * @param verbose if true the system will print back the new value of the configuration parameter
 * @return 0 on success
 */
static int
config_set(SSL* ssl, int socket, char* config_key, char* config_value, bool verbose)
{
   char* buffer = NULL;
   int status = EXIT_STATUS_DATA_ERROR;

   if (!config_key || strlen(config_key) > MISC_LENGTH
       || !config_value || strlen(config_value) > MISC_LENGTH)
   {
      goto error;
   }

   if (pgagroal_management_config_set(ssl, socket, config_key, config_value))
   {
      goto error;
   }
   else
   {
      buffer = malloc(MISC_LENGTH);
      memset(buffer, 0, MISC_LENGTH);
      if (pgagroal_management_read_config_get(socket, &buffer))
      {
         free(buffer);
         goto error;
      }

      // if the setting we sent is different from the setting we get
      // than the system has not applied, so it is an error
      if (strncmp(config_value, buffer, MISC_LENGTH) == 0)
      {
         status = EXIT_STATUS_OK;
      }
      else
      {
         status = EXIT_STATUS_DATA_ERROR;
      }

      // assume an empty response is ok,
      // do not throw an error to indicate no configuration
      // setting with such name as been found
      if (buffer && strlen(buffer))
      {
         if (verbose)
         {
            printf("%s = %s\n", config_key, buffer);
         }
         else
         {
            printf("%s\n", buffer);
         }
      }

      free(buffer);
      return status;
   }

   return status;
error:
   return EXIT_STATUS_CONNECTION_ERROR;
}
