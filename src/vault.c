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
#include <message.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <arpa/inet.h>
#include <err.h>
#include <getopt.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif

#define MAX_FDS 64

static void accept_vault_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents);
static bool accept_fatal(int error);
static int connect_pgagroal(struct vault_configuration* config, char* username, char* password, SSL** s_ssl, int* client_socket);
static void route_users(char* username, char** response, SSL* s_ssl, int client_fd);
static void route_not_found(char** response);
static void route_found(char** response, char* password);
static int router(SSL* ssl, int client_fd);

static volatile int keep_running = 1;
static char** argv_ptr;
static struct ev_loop* main_loop = NULL;
static struct accept_io io_main;
static int* server_fd = NULL;
static int server_fds_length = -1;
static int default_buffer_size = DEFAULT_BUFFER_SIZE;

static int
router(SSL* s_ssl, int client_fd)
{
   int exit_code = 0;
   ssize_t bytes_read;
   ssize_t bytes_write;
   char* body = NULL;
   char* response = NULL;
   char method[8];
   char path[128];
   char buffer[HTTP_BUFFER_SIZE];
   char contents[HTTP_BUFFER_SIZE];
   char username[MAX_USERNAME_LENGTH + 1]; // Assuming username is less than 128 characters

   memset(&response, 0, sizeof(response));

   // Read the request
   bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
   buffer[bytes_read] = '\0';

   sscanf(buffer, "%7s %127s", method, path);

   // Extract the POST data
   body = strstr(buffer, "\r\n\r\n");
   if (body)
   {
      strcpy(contents, body + 4);
   }

   // Parse URL parameters for GET requests only
   if (strcmp(method, "GET") == 0)
   {
      // Call the appropriate handler function for the URL path
      if (strncmp(path, "/users/", 7) == 0 && strcmp(method, "GET") == 0) // Only one '/'
      {
         // Extract the username from the path
         sscanf(path, "/users/%128s", username);
         // Call the appropriate handler function with the username
         route_users(username, &response, s_ssl, client_fd);
      }
      else
      {
         route_not_found(&response);
      }
   }
   else
   {
      route_not_found(&response);
   }

   // Send the response
   bytes_write = write(client_fd, response, strlen(response));

   if (bytes_write <= 0)
   {
      exit_code = 1;
   }

   free(response);

   return exit_code;
}

static void
route_users(char* username, char** response, SSL* s_ssl, int client_fd)
{
   struct vault_configuration* config = (struct vault_configuration*)shmem;
   int client_pgagroal_fd = -1;
   char password[MAX_PASSWORD_LENGTH + 1];

   // Connect to pgagroal management port
   if (connect_pgagroal(config, config->vault_server.user.username, config->vault_server.user.password, &s_ssl, &client_pgagroal_fd)) // Change NULL to ssl
   {
      pgagroal_log_error("pgagroal-vault: Couldn't connect to %s:%d", config->vault_server.server.host, config->vault_server.server.port);
      // Send Error Response
      route_not_found(response);
      return;
   }

   memset(password, 0, MAX_PASSWORD_LENGTH);

   // Call GET_PASSWORD at management port
   if (pgagroal_management_get_password(s_ssl, client_pgagroal_fd, username, password))
   {
      pgagroal_log_error("pgagroal-vault: Couldn't get password from the management");
      // Send Error Response
      route_not_found(response);
      return;
   }

   if (strlen(password) == 0) // user not found
   {
      pgagroal_log_warn("pgagroal-vault: Couldn't find the user: %s", username);
      route_not_found(response);
   }

   else
   {
      route_found(response, password);
   }
}

static void
route_not_found(char** response)
{
   char* tmp_response = NULL;
   memset(&tmp_response, 0, sizeof(tmp_response));
   tmp_response = pgagroal_append(tmp_response, "HTTP/1.1 404 Not Found\r\n\r\n");
   *response = tmp_response;
}

static void
route_found(char** response, char* password)
{
   char* tmp_response = NULL;
   memset(&tmp_response, 0, sizeof(tmp_response));
   tmp_response = pgagroal_append(tmp_response, "HTTP/1.1 200 OK\r\n");
   tmp_response = pgagroal_append(tmp_response, "Content-Type: text/plain\r\n");
   tmp_response = pgagroal_append(tmp_response, "\r\n\r\n");
   tmp_response = pgagroal_append(tmp_response, password);
   tmp_response = pgagroal_append(tmp_response, "\r\n");
   *response = tmp_response;
}

static int
connect_pgagroal(struct vault_configuration* config, char* username, char* password, SSL** s_ssl, int* client_socket)
{
   SSL* s = NULL;
   
   if (pgagroal_connect(config->vault_server.server.host, config->vault_server.server.port, client_socket, false, false, &default_buffer_size, false))
   {
      pgagroal_disconnect(*client_socket);
      return 1;
   }

   pgagroal_log_debug("connect_pgagroal: Authenticating the remote management access to %s:%d", config->vault_server.server.host, config->vault_server.server.port);
   username = config->vault_server.user.username;

   for (int i = 0; i < strlen(password); i++)
   {
      if ((unsigned char)(*(password + i)) & 0x80)
      {

         pgagroal_log_debug("pgagroal-vault: Bad credentials for %s", username);
         return 1;
      }
   }

   /* Authenticate */
   if (pgagroal_remote_management_scram_sha256(username, password, *client_socket, &s) != AUTH_SUCCESS)
   {
      pgagroal_log_debug("pgagroal-vault: Bad credentials for %s", username);
      pgagroal_disconnect(*client_socket);
      return 1;
   }

   *s_ssl = s;

   return 0;
}

static void
start_vault_io(void)
{
   int sockfd = *server_fd;

   memset(&io_main, 0, sizeof(struct accept_io));
   ev_io_init((struct ev_io*)&io_main, accept_vault_cb, sockfd, EV_READ);
   io_main.socket = sockfd;
   io_main.argv = argv_ptr;
   ev_io_start(main_loop, (struct ev_io*)&io_main);
}

static void
shutdown_vault_io(void)
{
   ev_io_stop(main_loop, (struct ev_io*)&io_main);
   pgagroal_disconnect(io_main.socket);
   errno = 0;
}

static void
usage(void)
{
   printf("pgagroal-vault %s\n", PGAGROAL_VERSION);
   printf("  Simple vault that hosts an HTTP server to handle user frontend password requests\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-vault [ -c CONFIG_FILE ] [ -u USERS_FILE ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE           Set the path to the pgagroal_vault.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_VAULT_CONF_FILE);
   printf("  -u, --users  USERS_FILE            Set the password for the admin user of management port\n");
   printf("  -?, --help                         Display help\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char** argv)
{
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   char* users_path = NULL;
   struct signal_info signal_watcher[1]; // Can add more
   int c;
   int option_index = 0;
   size_t size;
   struct vault_configuration* config = NULL;
   char message[MISC_LENGTH]; // a generic message used for errors

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"users", required_argument, 0, 'u'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "?c:u:",
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
         case 'u':
            users_path = optarg;
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
      errx(1, "pgagroal-vault: Using the root account is not allowed");
   }

   size = sizeof(struct vault_configuration);
   if (pgagroal_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      errx(1, "pgagroal-vault: Error creating shared memory");
   }

   memset(message, 0, MISC_LENGTH);

   pgagroal_vault_init_configuration(shmem);
   config = (struct vault_configuration*)shmem;

   configuration_path = configuration_path != NULL ? configuration_path : PGAGROAL_DEFAULT_VAULT_CONF_FILE;
   if ((ret = pgagroal_vault_read_configuration(shmem, configuration_path, false)) != PGAGROAL_CONFIGURATION_STATUS_OK)
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

      errx(1, "pgagroal-vault: %s (file <%s>)", message, configuration_path);
   }

   memcpy(&config->common.configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   if (pgagroal_init_logging())
   {
      exit(1);
   }

   if (pgagroal_start_logging())
   {
      errx(1, "Failed to start logging");
   }

   if (pgagroal_vault_validate_configuration(shmem))
   {
      errx(1, "pgagroal-vault: Invalid VAULT configuration");
   }

   config = (struct vault_configuration*)shmem;

   // -- Read the USERS file --
read_users_path:
   if (users_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_vault_read_users_configuration(shmem, users_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {

         snprintf(message, MISC_LENGTH, "USERS configuration file not found");
         errx(1, "pgagroal-vault: %s (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT)
      {
         errx(1, "pgagroal-vault: Invalid entry in the file");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_ADMINS);
         errx(1, "pgagroal-vault: %s (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      users_path = PGAGROAL_DEFAULT_VAULT_USERS_FILE;
      goto read_users_path;
   }

   // -- Bind & Listen at the given hostname and port --

   if (pgagroal_bind(config->common.host, config->common.port, &server_fd, &server_fds_length, false, &default_buffer_size, false, -1))
   {
      errx(1, "pgagroal-vault: Could not bind to %s:%d", config->common.host, config->common.port);
   }

   // -- Initialize the watcher and start loop --
   main_loop = ev_default_loop(0);

   if (!main_loop)
   {
      errx(1, "pgagroal-vault: No loop implementation");
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);

   for (int i = 0; i < 1; i++)
   {
      signal_watcher[i].slot = -1;
      ev_signal_start(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   start_vault_io();

   pgagroal_log_info("pgagroal-vault %s: Started on %s:%d",
                     PGAGROAL_VERSION,
                     config->common.host,
                     config->common.port);

   while (keep_running)
   {
      ev_loop(main_loop, 0);
   }

   pgagroal_log_info("pgagroal-vault: shutdown");
   // -- Free all memory --
   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(shmem, size);
   free(server_fd);

   return exit_code;
}

static void
shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgagroal_log_debug("pgagroal-vault: Shutdown requested");
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
accept_vault_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   pid_t pid;
   SSL* s_ssl = NULL;
   struct vault_configuration* config;

   if (EV_ERROR & revents)
   {
      pgagroal_log_debug("accept_vault_cb: Invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct vault_configuration*)shmem;

   memset(&address, 0, sizeof(address));

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgagroal_log_warn("accept_vault_cb: Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_vault_io();

         free(server_fd);
         server_fd = NULL;

         if (pgagroal_bind(config->common.host, config->common.port, &server_fd, &server_fds_length, false, &default_buffer_size, false, -1))
         {
            pgagroal_log_fatal("pgagroal-vault: Could not bind to %s:%d", config->common.host, config->common.port);
            exit(1);
         }

         if (!fork())
         {
            shutdown_vault_io();
         }

         start_vault_io();
         pgagroal_log_debug("Socket: %d", *server_fd);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }
   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   pgagroal_log_trace("accept_vault_cb: client address: %s", address);

   pid = fork();
   if (pid == -1)
   {
      /* No process */
      pgagroal_log_error("accept_vault_cb: Couldn't create process");
   }
   else if (pid == 0)
   {
      char* addr = calloc(1, strlen(address) + 1);
      if (addr == NULL)
      {
         pgagroal_log_fatal("accept_vault_cb: Couldn't allocate memory for client address");
         return;
      }
      memcpy(addr, address, strlen(address));

      ev_loop_fork(loop);
      shutdown_vault_io();

      if (router(s_ssl, client_fd))
      {
         pgagroal_log_error("Couldn't write to client");
         exit(1);
      }

      exit(0);
   }

   pgagroal_disconnect(client_fd);
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