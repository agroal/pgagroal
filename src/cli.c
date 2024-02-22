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
#define ACTION_STATUS_DETAILS  5
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
#define ACTION_CONFIG_LS      16

static int flush(SSL* ssl, int socket, int32_t mode, char* database);
static int enabledb(SSL* ssl, int socket, char* database);
static int disabledb(SSL* ssl, int socket, char* database);
static int gracefully(SSL* ssl, int socket);
static int stop(SSL* ssl, int socket);
static int cancel_shutdown(SSL* ssl, int socket);
static int status(SSL* ssl, int socket, char output_format);
static int details(SSL* ssl, int socket, char output_format);
static int isalive(SSL* ssl, int socket, char output_format);
static int reset(SSL* ssl, int socket);
static int reset_server(SSL* ssl, int socket, char* server);
static int switch_to(SSL* ssl, int socket, char* server);
static int reload(SSL* ssl, int socket);
static int config_ls(SSL* ssl, int socket, char output_format);
static int config_get(SSL* ssl, int socket, char* config_key, bool verbose, char output_format);
static int config_set(SSL* ssl, int socket, char* config_key, char* config_value, bool verbose, char output_format);

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
   printf("  pgagroal-cli [ OPTIONS ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file\n");
   printf("                           Default: %s\n", PGAGROAL_DEFAULT_CONF_FILE);
   printf("  -h, --host HOST          Set the host name\n");
   printf("  -p, --port PORT          Set the port number\n");
   printf("  -U, --user USERNAME      Set the user name\n");
   printf("  -P, --password PASSWORD  Set the password\n");
   printf("  -L, --logfile FILE       Set the log file\n");
   printf("  -F, --format text|json   Set the output format\n");
   printf("  -v, --verbose            Output text string of result\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  flush [mode] [database]  Flush connections according to [mode].\n");
   printf("                           Allowed modes are:\n");
   printf("                           - 'gracefully' (default) to flush all connections gracefully\n");
   printf("                           - 'idle' to flush only idle connections\n");
   printf("                           - 'all' to flush all connections. USE WITH CAUTION!\n");
   printf("                           If no [database] name is specified, applies to all databases.\n");
   printf("  ping                     Verifies if pgagroal is up and running\n");
   printf("  enable   [database]      Enables the specified databases (or all databases)\n");
   printf("  disable  [database]      Disables the specified databases (or all databases)\n");
   printf("  shutdown [mode]          Stops pgagroal pooler. The [mode] can be:\n");
   printf("                           - 'gracefully' (default) waits for active connections to quit\n");
   printf("                           - 'immediate' forces connections to close and terminate\n");
   printf("                           - 'cancel' avoid a previously issued 'shutdown gracefully'\n");
   printf("  status [details]         Status of pgagroal, with optional details\n");
   printf("  switch-to [server]       Switches to the specified primary server\n");
   printf("  conf [action[            Manages the configuration (e.g., reloads the configuration\n");
   printf("                           The subcommand [action] can be:\n");
   printf("                           - 'reload' to issue a configuration reload;\n");
   printf("                           - 'get' to obtain information about a runtime configuration value;\n");
   printf("                                   conf get <parameter_name>\n");
   printf("                           - 'set' to modify a configuration value;\n");
   printf("                                   conf set <parameter_name> <parameter_value>;\n");
   printf("                           - 'ls'  lists the configuration files used.\n");
   printf("  clear [what]             Resets either the Prometheus statistics or the specified server.\n");
   printf("                           [what] can be\n");
   printf("                           - 'server' (default) followed by a server name\n");
   printf("                           - a server name on its own\n");
   printf("                           - 'prometheus' to reset the Prometheus metrics\n");
   printf("\n");
   printf("pgagroal: <%s>\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: <%s>\n", PGAGROAL_ISSUES);
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
   char output_format = COMMAND_OUTPUT_FORMAT_TEXT;

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
         {"format", required_argument, 0, 'F' },
         {"verbose", no_argument, 0, 'v'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:F:",
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
         case 'F':
            if (!strncmp(optarg, "json", MISC_LENGTH))
            {
               output_format = COMMAND_OUTPUT_FORMAT_JSON;
            }
            else
            {
               output_format = COMMAND_OUTPUT_FORMAT_TEXT;
            }
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

   if (parse_command(argc, argv, optind, "flush", "idle", &database, "*", NULL, NULL)
       || parse_deprecated_command(argc, argv, optind, "flush-idle", &database, "flush idle", 1, 6))
   {
      mode = FLUSH_IDLE;
      action = ACTION_FLUSH;
      pgagroal_log_trace("Command: <flush idle> [%s]", database);
   }
   else if (parse_command(argc, argv, optind, "flush", "all", &database, "*", NULL, NULL)
            || parse_deprecated_command(argc, argv, optind, "flush-all", &database, "flush all", 1, 6))
   {
      mode = FLUSH_ALL;
      action = ACTION_FLUSH;
      pgagroal_log_trace("Command: <flush all> [%s]", database);
   }
   else if (parse_command(argc, argv, optind, "flush", "gracefully", &database, "*", NULL, NULL)
            || parse_command(argc, argv, optind, "flush", NULL, &database, "*", NULL, NULL)
            || parse_deprecated_command(argc, argv, optind, "flush-gracefully", &database, "flush", 1, 6))
   {
      mode = FLUSH_GRACEFULLY;
      action = ACTION_FLUSH;
      pgagroal_log_trace("Command: <flush gracefully> [%s]", database);
   }
   else if (parse_command(argc, argv, optind, "enable", NULL, &database, "*", NULL, NULL))
   {
      action = ACTION_ENABLEDB;
      pgagroal_log_trace("Command: <enable> [%s]", database);
   }
   else if (parse_command(argc, argv, optind, "disable", NULL, &database, "*", NULL, NULL))
   {
      action = ACTION_DISABLEDB;
      pgagroal_log_trace("Command: <disable> [%s]", database);
   }
   else if (parse_command_simple(argc, argv, optind, "shutdown", "immediate")
            || parse_deprecated_command(argc, argv, optind, "stop", NULL, "shutdown immediate", 1, 6))
   {
      action = ACTION_STOP;
      pgagroal_log_trace("Command: <shutdown immediate>");
   }
   else if (parse_command_simple(argc, argv, optind, "shutdown", "cancel")
            || parse_deprecated_command(argc, argv, optind, "cancel-shutdown", NULL, "shutdown cancel", 1, 6))
   {
      action = ACTION_CANCELSHUTDOWN;
      pgagroal_log_trace("Command: <shutdown cancel>");
   }
   else if (parse_command_simple(argc, argv, optind, "shutdown", "gracefully")
            || parse_command_simple(argc, argv, optind, "shutdown", NULL)
            || parse_deprecated_command(argc, argv, optind, "gracefully", NULL, "shutdown gracefully", 1, 6))
   {
      action = ACTION_GRACEFULLY;
      pgagroal_log_trace("Command: <shutdown gracefully>");
   }
   else if (parse_command_simple(argc, argv, optind, "status", "details")
            || parse_deprecated_command(argc, argv, optind, "details", NULL, "status details", 1, 6))
   {
      /* the 'status details' has to be parsed before the normal 'status' command !*/
      action = ACTION_STATUS_DETAILS;
      pgagroal_log_trace("Command: <status details>");
   }
   else if (parse_command_simple(argc, argv, optind, "status", NULL))
   {
      action = ACTION_STATUS;
      pgagroal_log_trace("Command: <status>");
   }
   else if (parse_command_simple(argc, argv, optind, "ping", NULL)
            || parse_deprecated_command(argc, argv, optind, "is-alive", NULL, "ping", 1, 6))
   {
      action = ACTION_ISALIVE;
      pgagroal_log_trace("Command: <is-alive>");
   }
   else if (parse_command_simple(argc, argv, optind, "clear", "prometheus")
            || parse_deprecated_command(argc, argv, optind, "reset", NULL, "clear prometheus", 1, 6))
   {
      action = ACTION_RESET;
      pgagroal_log_trace("Command: <clear prometheus>");
   }
   else if (parse_command(argc, argv, optind, "clear", "server", &server, "\0", NULL, NULL)
            || parse_command(argc, argv, optind, "clear", NULL, &server, "\0", NULL, NULL)
            || parse_deprecated_command(argc, argv, optind, "reset-server", &server, "clear server", 1, 6))
   {
      action = strlen(server) > 0 ? ACTION_RESET_SERVER : ACTION_UNKNOWN;
      pgagroal_log_trace("Command: <clear server> [%s]", server);
   }
   else if (parse_command(argc, argv, optind, "switch-to", NULL, &server, "\0", NULL, NULL))
   {
      action = strlen(server) > 0 ? ACTION_SWITCH_TO : ACTION_UNKNOWN;
      pgagroal_log_trace("Command: <switch-to> [%s]", server);
   }
   else if (parse_command_simple(argc, argv, optind, "conf", "reload")
            || parse_deprecated_command(argc, argv, optind, "reload", NULL, "conf reload", 1, 6))
   {
      /* Local connection only */
      if (configuration_path != NULL)
      {
         action = ACTION_RELOAD;
      }
      pgagroal_log_trace("Command: <reload>");
   }
   else if (parse_command(argc, argv, optind, "conf", "get", &config_key, NULL, NULL, NULL)
            || parse_deprecated_command(argc, argv, optind, "config-get", NULL, "conf get", 1, 6))
   {
      action = config_key != NULL && strlen(config_key) > 0 ? ACTION_CONFIG_GET : ACTION_UNKNOWN;
      pgagroal_log_trace("Command: <conf get> [%s]", config_key);
   }
   else if (parse_command(argc, argv, optind, "conf", "set", &config_key, NULL, &config_value, NULL)
            || parse_deprecated_command(argc, argv, optind, "config-set", NULL, "conf set", 1, 6))
   {
      // if there is no configuration key set the action to unknown, so the help screen will be printed
      action = config_key != NULL && strlen(config_key) > 0 ? ACTION_CONFIG_SET : ACTION_UNKNOWN;
      pgagroal_log_trace("Command: <conf set> [%s] = [%s]", config_key, config_value);
   }
   else if (parse_command_simple(argc, argv, optind, "conf", "ls"))
   {
      pgagroal_log_debug("Command: <conf ls>");
      action = ACTION_CONFIG_LS;
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
         if (pgagroal_connect(host, atoi(port), &socket))
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

               warnx("Bad credentials for %s\n", username);
               goto done;
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
      exit_code = status(s_ssl, socket, output_format);
   }
   else if (action == ACTION_STATUS_DETAILS)
   {
      exit_code = details(s_ssl, socket, output_format);
   }
   else if (action == ACTION_ISALIVE)
   {
      exit_code = isalive(s_ssl, socket, output_format);
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
      exit_code = config_get(s_ssl, socket, config_key, verbose, output_format);
   }
   else if (action == ACTION_CONFIG_SET)
   {
      exit_code = config_set(s_ssl, socket, config_key, config_value, verbose, output_format);
   }
   else if (action == ACTION_CONFIG_LS)
   {
      exit_code = config_ls(s_ssl, socket, output_format);
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
      printf("pgagroal-cli: unknown command %s\n", argv[optind]);
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
status(SSL* ssl, int socket, char output_format)
{
   if (pgagroal_management_status(ssl, socket) == 0)
   {
      return pgagroal_management_read_status(ssl, socket, output_format);
   }
   else
   {
      return EXIT_STATUS_CONNECTION_ERROR;
   }
}

static int
details(SSL* ssl, int socket, char output_format)
{
   if (pgagroal_management_details(ssl, socket) == 0)
   {
      return pgagroal_management_read_details(ssl, socket, output_format);

   }

   // if here, an error occurred
   return EXIT_STATUS_CONNECTION_ERROR;

}

static int
isalive(SSL* ssl, int socket, char output_format)
{
   int status = -1;

   if (pgagroal_management_isalive(ssl, socket) == 0)
   {
      if (pgagroal_management_read_isalive(ssl, socket, &status, output_format))
      {
         return EXIT_STATUS_CONNECTION_ERROR;
      }

      if (status != PING_STATUS_RUNNING && status != PING_STATUS_SHUTDOWN_GRACEFULLY)
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
 * @param output_format the format for the output (e.g., json)
 * @returns 0 on success, 1 on network failure, 2 on data failure
 */
static int
config_get(SSL* ssl, int socket, char* config_key, bool verbose, char output_format)
{

   if (!config_key || strlen(config_key) > MISC_LENGTH)
   {
      goto error;
   }

   if (pgagroal_management_config_get(ssl, socket, config_key))
   {
      goto error;
   }

   if (pgagroal_management_read_config_get(socket, config_key, NULL, verbose, output_format))
   {
      goto error;
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
config_set(SSL* ssl, int socket, char* config_key, char* config_value, bool verbose, char output_format)
{

   int status = EXIT_STATUS_OK;

   if (!config_key || strlen(config_key) > MISC_LENGTH
       || !config_value || strlen(config_value) > MISC_LENGTH)
   {
      goto error;
   }

   if (pgagroal_management_config_set(ssl, socket, config_key, config_value))
   {
      goto error;
   }

   status = pgagroal_management_read_config_get(socket, config_key, config_value, verbose, output_format);

   return status;
error:
   return EXIT_STATUS_CONNECTION_ERROR;
}

/**
 * Asks the daemon about the configuration file location.
 *
 * @returns 0 on success
 */
static int
config_ls(SSL* ssl, int socket, char output_format)
{

   if (pgagroal_management_conf_ls(ssl, socket))
   {
      goto error;
   }

   if (pgagroal_management_read_conf_ls(ssl, socket, output_format))
   {
      goto error;
   }

   return EXIT_STATUS_OK;
error:
   return EXIT_STATUS_CONNECTION_ERROR;
}
