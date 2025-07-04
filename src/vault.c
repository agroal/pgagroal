/*
 * Copyright (C) 2025 The pgagroal community
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
#include <ev.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <prometheus.h>

/* system */
#include <arpa/inet.h>
#include <err.h>
#include <getopt.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define CLIENTSSL_ON_SERVERSSL_ON   0
#define CLIENTSSL_ON_SERVERSSL_OFF  1
#define CLIENTSSL_OFF_SERVERSSL_ON  2
#define CLIENTSSL_OFF_SERVERSSL_OFF 3

#define MAX_FDS 64

static void accept_vault_cb(struct io_watcher* watcher);
static void accept_metrics_cb(struct io_watcher* watcher);
static void shutdown_cb(void);
static bool accept_fatal(int error);
static int connect_pgagroal(struct vault_configuration* config, char* username, char* password, SSL** s_ssl, int* client_socket);
static void route_users(char* username, char** response, SSL* s_ssl, int client_fd);
static void route_not_found(char** response);
static void route_found(char** response, char* password);
static void route_redirect(char** response, char* redirect_link);
static int router(SSL* ccl, SSL* ssl, int client_fd);
static int get_connection_state(struct vault_configuration* config, int client_fd);

static char** argv_ptr;
static struct event_loop* main_loop = NULL;
static struct accept_io io_main[MAX_FDS];
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static int* server_fds = NULL;
static int server_fds_length = -1;

static int
router(SSL* c_ssl, SSL* s_ssl, int client_fd)
{
   int exit_code = 0;
   int connection_state;
   ssize_t bytes_write;
   struct vault_configuration* config;
   char* response = NULL;
   char method[8];
   char path[128];
   char buffer[HTTP_BUFFER_SIZE];
   char username[MAX_USERNAME_LENGTH + 1]; // Assuming username is less than 128 characters
   char* redirect_link = NULL;

   config = (struct vault_configuration*)shmem;
   memset(&response, 0, sizeof(response));
   memset(&buffer, 0, sizeof(buffer));

   connection_state = get_connection_state(config, client_fd);
   switch (connection_state)
   {
      case CLIENTSSL_ON_SERVERSSL_ON:
         if (accept_ssl_vault(config, client_fd, &c_ssl))
         {
            pgagroal_log_error("accept_ssl_vault: SSL connection failed");
            exit_code = 1;
            goto exit;
         }
         pgagroal_read_socket(c_ssl, client_fd, buffer, sizeof(buffer));
         sscanf(buffer, "%7s %127s", method, path);
         break;
      case CLIENTSSL_OFF_SERVERSSL_ON:
         pgagroal_read_socket(c_ssl, client_fd, buffer, sizeof(buffer));
         sscanf(buffer, "%7s %127s", method, path);
         redirect_link = pgagroal_append(redirect_link, "https://");
         redirect_link = pgagroal_append(redirect_link, config->common.host);
         redirect_link = pgagroal_append(redirect_link, ":");
         redirect_link = pgagroal_append_int(redirect_link, config->common.port);
         redirect_link = pgagroal_append(redirect_link, path);
         route_redirect(&response, redirect_link);
         pgagroal_log_error("client must initiate tls handshake");
         goto send;
      case CLIENTSSL_OFF_SERVERSSL_OFF:
         pgagroal_read_socket(c_ssl, client_fd, buffer, sizeof(buffer));
         sscanf(buffer, "%7s %127s", method, path);
         break;
      case CLIENTSSL_ON_SERVERSSL_OFF:
         pgagroal_log_error("client requests tls connection to http server");
         __attribute__ ((fallthrough));
      default:
         return 1;
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

send:
   // Send the response
   bytes_write = pgagroal_write_socket(c_ssl, client_fd, response, strlen(response));
   if (bytes_write <= 0)
   {
      exit_code = 1;
   }
   pgagroal_prometheus_client_sockets_sub();
   free(response);
exit:
   return exit_code;
}

static void
route_users(char* username, char** response, SSL* s_ssl, int client_fd __attribute__((unused)))
{
   struct vault_configuration* config = (struct vault_configuration*)shmem;
   int client_pgagroal_fd = -1;
   struct json* read = NULL;
   struct json* res = NULL;
   char* password = NULL;

   // Connect to pgagroal management port
   if (connect_pgagroal(config, config->vault_server.user.username, config->vault_server.user.password, &s_ssl, &client_pgagroal_fd)) // Change NULL to ssl
   {
      pgagroal_log_error("pgagroal-vault: Couldn't connect to %s:%d", config->vault_server.server.host, config->vault_server.server.port);
      // Send Error Response
      route_not_found(response);
      return;
   }

   // Call GET_PASSWORD at management port
   if (pgagroal_management_request_get_password(s_ssl, client_pgagroal_fd, username, COMPRESSION_NONE, ENCRYPTION_AES_256_CBC, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      pgagroal_log_error("pgagroal-vault: Couldn't get password from the management");
      // Send Error Response
      route_not_found(response);
      return;
   }

   if (pgagroal_management_read_json(s_ssl, client_pgagroal_fd, NULL, NULL, &read))
   {
      pgagroal_log_warn("pgagroal-vault: Couldn't receive the result");
   }

   if (read != NULL)
   {
      res = (struct json*)pgagroal_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);
      password = (char*)pgagroal_json_get(res, MANAGEMENT_ARGUMENT_PASSWORD);
   }

   if (password == NULL || strlen(password) == 0) // user not found
   {
      pgagroal_log_warn("pgagroal-vault: Couldn't find the user: %s", username);
      route_not_found(response);
   }
   else
   {
      route_found(response, password);
   }

   pgagroal_json_destroy(read);
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

static void
route_redirect(char** response, char* redirect_link)
{
   char* tmp_response = NULL;
   tmp_response = pgagroal_append(tmp_response, "HTTP/1.1 301 Moved Permanently\r\n");
   tmp_response = pgagroal_append(tmp_response, "Content-Length: 0\r\n");
   tmp_response = pgagroal_append(tmp_response, "Location: ");
   tmp_response = pgagroal_append(tmp_response, redirect_link);
   tmp_response = pgagroal_append(tmp_response, "\r\n");
   *response = tmp_response;
}

static int
connect_pgagroal(struct vault_configuration* config, char* username, char* password, SSL** s_ssl, int* client_socket)
{
   SSL* s = NULL;

   if (pgagroal_connect(config->vault_server.server.host, config->vault_server.server.port, client_socket, false, false))
   {
      pgagroal_disconnect(*client_socket);
      return 1;
   }

   pgagroal_log_debug("connect_pgagroal: Authenticating the remote management access to %s:%d", config->vault_server.server.host, config->vault_server.server.port);
   username = config->vault_server.user.username;

   for (unsigned long i = 0; i < strlen(password); i++)
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

static int
get_connection_state(struct vault_configuration* config, int client_fd)
{
   int is_ssl_req = pgagroal_is_ssl_request(client_fd);
   if (config->common.tls)
   {
      if (is_ssl_req)
      {
         return CLIENTSSL_ON_SERVERSSL_ON;
      }
      else
      {
         return CLIENTSSL_OFF_SERVERSSL_ON;
      }
   }
   else if (is_ssl_req)
   {
      return CLIENTSSL_ON_SERVERSSL_OFF;
   }
   return CLIENTSSL_OFF_SERVERSSL_OFF;
}

static void
start_vault_io(void)
{
   for (int i = 0; i < server_fds_length; i++)
   {
      int sockfd = *(server_fds + i);

      memset(&io_main[i], 0, sizeof(struct accept_io));
      pgagroal_event_accept_init(&io_main[i].watcher, sockfd, accept_vault_cb);
      io_main[i].socket = sockfd;
      io_main[i].argv = argv_ptr;
      pgagroal_io_start(&io_main[i].watcher);
   }
}

static void
shutdown_vault_io(void)
{
   for (int i = 0; i < server_fds_length; i++)
   {
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
      pgagroal_event_accept_init(&io_metrics[i].watcher, sockfd, accept_metrics_cb);
      io_metrics[i].socket = sockfd;
      io_metrics[i].argv = argv_ptr;
      pgagroal_io_start(&io_metrics[i].watcher);
   }
}

static void
shutdown_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_disconnect(io_metrics[i].socket);
      errno = 0;
   }
}

static void
shutdown_ports(void)
{
   struct vault_configuration* config;

   config = (struct vault_configuration*)shmem;

   shutdown_vault_io();

   if (config->common.metrics > 0)
   {
      shutdown_metrics();
   }
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
   size_t prometheus_shmem_size = 0;
   size_t prometheus_cache_shmem_size = 0;
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

   if (config->common.metrics > 0)
   {
      if (pgagroal_vault_init_prometheus(&prometheus_shmem_size, &prometheus_shmem))
      {
   #ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Error in creating and initializing prometheus shared memory");
   #endif
         errx(1, "Error in creating and initializing prometheus shared memory");
      }

      if (pgagroal_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
      {
   #ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
   #endif
         errx(1, "Error in creating and initializing prometheus cache shared memory");
      }
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

   if (pgagroal_bind(config->common.host, config->common.port, &server_fds, &server_fds_length, false, -1))
   {
      errx(1, "pgagroal-vault: Could not bind to %s:%d", config->common.host, config->common.port);
   }

   if (server_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", server_fds_length);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Too many descriptors %d", server_fds_length);
#endif
      exit(1);
   }

//-- Initialize the watcher and start loop --
   pgagroal_event_set_context(PGAGROAL_CONTEXT_VAULT);
   main_loop = pgagroal_event_loop_init();
   if (!main_loop)
   {
      errx(1, "pgagroal-vault: No loop implementation");
   }

   pgagroal_signal_init((struct signal_watcher*)&signal_watcher[0], shutdown_cb, SIGTERM);

   for (int i = 0; i < 1; i++)
   {
      signal_watcher[i].slot = -1;
      pgagroal_signal_start((struct signal_watcher*)&signal_watcher[i]);
   }

   start_vault_io();

   if (config->common.metrics > 0)
   {
      /* Bind metrics socket */
      if (pgagroal_bind(config->common.host, config->common.metrics, &metrics_fds, &metrics_fds_length, false, -1))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.metrics);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->common.metrics);
#endif
         exit(1);
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         exit(1);
      }

      start_metrics();
   }

   pgagroal_log_info("pgagroal-vault %s: Started on %s:%d",
                     PGAGROAL_VERSION,
                     config->common.host,
                     config->common.port);
   for (int i = 0; i < server_fds_length; i++)
   {
      pgagroal_log_debug("Socket: %d", *(server_fds + i));
   }
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
   }

   pgagroal_event_loop_run();

   pgagroal_log_info("pgagroal-vault: shutdown");

   shutdown_ports();

   pgagroal_event_loop_destroy();

   // -- Free all memory --
   free(metrics_fds);
   free(server_fds);
   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(shmem, size);

   return exit_code;
}

static void
shutdown_cb(void)
{
   pgagroal_log_debug("pgagroal-vault: Shutdown requested");
   pgagroal_event_loop_break();

}

static void
accept_vault_cb(struct io_watcher* watcher)
{
   struct sockaddr_in6 client_addr;

   int client_fd;
   char address[INET6_ADDRSTRLEN];
   pid_t pid;
   SSL* c_ssl = NULL;
   SSL* s_ssl = NULL;
   struct vault_configuration* config;

   config = (struct vault_configuration*)shmem;

   memset(&address, 0, sizeof(address));

   client_fd = watcher->fds.main.client_fd;

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && pgagroal_event_loop_is_running())
      {
         pgagroal_log_warn("accept_vault_cb: Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_vault_io();

         free(server_fds);
         server_fds = NULL;

         if (pgagroal_bind(config->common.host, config->common.port, &server_fds, &server_fds_length, false, -1))
         {
            pgagroal_log_fatal("pgagroal-vault: Could not bind to %s:%d", config->common.host, config->common.port);
            exit(1);
         }

         if (!fork())
         {
            pgagroal_event_loop_fork();
            shutdown_ports();
         }

         start_vault_io();
         pgagroal_log_debug("Socket: %d", *server_fds);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   pgagroal_prometheus_client_sockets_add();
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

      pgagroal_event_loop_fork();
      shutdown_ports();

      // Handle http request
      if (router(c_ssl, s_ssl, client_fd))
      {
         pgagroal_log_error("Couldn't write to client");
         pgagroal_disconnect(client_fd);
         exit(1);
      }

      exit(0);
   }

   pgagroal_disconnect(client_fd);
}

static void
accept_metrics_cb(struct io_watcher* watcher)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   struct vault_configuration* config;
   SSL_CTX* ctx = NULL;
   SSL* client_ssl = NULL;

   config = (struct vault_configuration*)shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fds.main.listen_fd, (struct sockaddr*)&client_addr, &client_addr_length);

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && pgagroal_event_loop_is_running())
      {
         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_metrics();

         free(metrics_fds);
         metrics_fds = NULL;
         metrics_fds_length = 0;

         if (pgagroal_bind(config->common.host, config->common.metrics, &metrics_fds, &metrics_fds_length, false, -1))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.metrics);
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
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   if (!fork())
   {
      pgagroal_event_loop_fork();
      shutdown_ports();
      if (strlen(config->common.metrics_cert_file) > 0 && strlen(config->common.metrics_key_file) > 0)
      {
         if (pgagroal_create_ssl_ctx(false, &ctx))
         {
            pgagroal_log_error("Could not create metrics SSL context");
            return;
         }

         if (pgagroal_create_ssl_server(ctx, config->common.metrics_key_file, config->common.metrics_cert_file, config->common.metrics_ca_file, client_fd, &client_ssl))
         {
            pgagroal_log_error("Could not create metrics SSL server");
            return;
         }
      }
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_vault_prometheus(client_ssl, client_fd);
   }

   pgagroal_disconnect(client_fd);
   pgagroal_prometheus_self_sockets_sub();
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
