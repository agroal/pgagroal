#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "uthash.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <err.h>
#include <getopt.h>
#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif


#include <pgagroal.h>
#include <logging.h>
#include <utils.h>
#include <message.h>
#include <memory.h>
#include <shmem.h>
#include <configuration.h>

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define BUFFER_SIZE 1024

struct KV
{
   char *key;
   char *value;
   UT_hash_handle hh;
};

static void initialize_ssl(void);
static SSL_CTX *create_ssl_context(void);
static void configure_ssl_context(SSL_CTX *ctx);
static void parse_query_string(const char *query_string, struct KV **hash_map);
static void free_hash_map(struct KV **hash_map);
static void handle(int client_fd, SSL *ssl, int is_http);
static void handle_url(const char *buffer, int client_fd, SSL *ssl, int is_http);
static void *http_thread_func(void *arg);
static void *https_thread_func(void *arg);
static void handle_users(const char *method, char *username, struct KV *params, char *response, size_t response_size);
static void handle_info(const char *method, const char *contents, char *response, size_t response_size);
static void handle_default(char *response, size_t response_size);

static SSL_CTX *ssl_ctx;
static int client_socket;

static void initialize_ssl(void)
{
   SSL_library_init();
   SSL_load_error_strings();
   OpenSSL_add_all_algorithms();
}

static SSL_CTX *create_ssl_context()
{
   const SSL_METHOD *method;
   SSL_CTX *ctx;

   method = SSLv23_server_method();
   ctx = SSL_CTX_new(method);
   if (ctx == NULL)
   {
      pgagroal_log_error("pgagroal_vault: failed to create ssl context");
      exit(EXIT_FAILURE);
   }

   return ctx;
}

static void configure_ssl_context(SSL_CTX *ctx)
{
   char crt[100];
   char key[100];
   sprintf(crt, "%s/.pgagroal/server.crt", pgagroal_get_home_directory());
   sprintf(key, "%s/.pgagroal/server.key", pgagroal_get_home_directory());
   if (SSL_CTX_use_certificate_file(ctx, crt, SSL_FILETYPE_PEM) <= 0)
   {
      pgagroal_log_error("pgagroal_vault: failed to use cert file %s", "server.crt");
      exit(EXIT_FAILURE);
   }

   if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0)
   {
      pgagroal_log_error("pgagroal_vault: failed to use cert file %s", "server.key");
      exit(EXIT_FAILURE);
   }
}

static void parse_query_string(const char *path, struct KV **map)
{
   char *token, *key, *value, *saveptr1, *saveptr2, *query;
   query = strchr(path, '?');
   if (query)
   {
      *query = '\0';
      token = strtok_r(query + 1, "&", &saveptr1);
      while (token)
      {
         key = strtok_r(token, "=", &saveptr2);
         value = strtok_r(NULL, "=", &saveptr2);
         struct KV *kv = malloc(sizeof(struct KV));
         kv->key = strcpy(malloc(strlen(key)), key);
         kv->value = strcpy(malloc(strlen(value)), value);
         HASH_ADD_STR(*map, key, kv);
         token = strtok_r(NULL, "&", &saveptr1);
      }
   }
}

static void free_hash_map(struct KV **hash_map)
{
   struct KV *entry, *tmp;
   HASH_ITER(hh, *hash_map, entry, tmp)
   {
      HASH_DEL(*hash_map, entry);
      free((void *)entry->key);
      free((void *)entry->value);
      free(entry);
   }
}

static void handle(int client_fd, SSL *ssl, int is_http)
{
   char buffer[BUFFER_SIZE];
   ssize_t bytes_read;
   if (is_http)
   {
      bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
   }
   else
   {
      bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
   }
   if (bytes_read <= 0)
   {
      if (!is_http)
      {
         SSL_free(ssl);
      }
      close(client_fd);
      return;
   }
   buffer[bytes_read] = '\0';
   handle_url(buffer, client_fd, ssl, is_http);
}

static void handle_url(const char *buffer, int client_fd, SSL *ssl, int is_http)
{
   char method[8];
   char path[128];
   char contents[BUFFER_SIZE];
   char response[BUFFER_SIZE];
   char format_string[BUFFER_SIZE];

   sscanf(buffer, "%7s %127s", method, path);

   // Extract the POST data
   char *body = strstr(buffer, "\r\n\r\n");
   if (body)
   {
      strcpy(contents, body + 4);
   }

   struct KV *map = NULL;

   // Parse URL parameters for GET requests
   if (strcmp(method, "GET") == 0)
   {
      parse_query_string(path, &map);
   }

   // Call the appropriate handler function for the URL path
   if (strncmp(path, "/users/", 7) == 0 && strcmp(method, "GET") == 0)
   {
      // Extract the username from the path
      char username[MAX_USERNAME_LENGTH + 1]; // Assuming username is less than 120 characters
      snprintf(format_string, sizeof(format_string), "/users/%%%ds", MAX_USERNAME_LENGTH);
      sscanf(path, "/users/%128s", username);

      // Call the appropriate handler function with the username
      // handle_users(username, response, sizeof(response));
      handle_users(method, username, map, response, sizeof(response));
   }   
   else if (strcmp(path, "/info") == 0)
   {
      handle_info(method, contents, response, sizeof(response));
   }
   else
   {
      handle_default(response, sizeof(response));
   }

   // Send the response
   if (is_http)
   {
      write(client_fd, response, strlen(response));
   }
   else
   {
      SSL_write(ssl, response, strlen(response));
      SSL_shutdown(ssl);
      SSL_free(ssl);
   }

   close(client_fd);
   free_hash_map(&map);
}

static void handle_users(const char *method, char *username, struct KV *params, char *response, size_t response_size)
{
   if (strcmp(method, "GET") == 0)
   {
      struct KV *kv;
      HASH_FIND_STR(params, "username", kv);
      struct message *msg;
      pgagroal_write_frontend_password_request(NULL, client_socket, username);
      pgagroal_memory_init();
      pgagroal_read_socket_message(client_socket, &msg);
      if(strcmp((char *)msg->data, "") != 0) {
         snprintf(response, response_size, "HTTP/1.1 200 OK\r\n"
                                          "Content-Type: text/plain\r\n"
                                          "\r\n"
                                          "Your temporary password is: %s\r\n",
                  (char *)msg->data);
      } else {
         snprintf(response, response_size, "HTTP/1.1 200 OK\r\n"
                                          "Content-Type: text/plain\r\n"
                                          "\r\n"
                                          "No such user\r\n");
      }
      pgagroal_memory_destroy();
   }
   else
   {
      snprintf(response, response_size, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
   }
}

static void handle_info(const char *method, const char *contents, char *response, size_t response_size)
{
   snprintf(response, response_size, "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "\r\n"
                                     "vault is good\r\n");
}

static void handle_default(char *response, size_t response_size)
{
   snprintf(response, response_size, "HTTP/1.1 404 Not Found\r\n\r\n");
}

static void *http_thread_func(void *arg)
{
   int server_fd, client_fd;
   struct sockaddr_in server_addr, client_addr;
   socklen_t client_len = sizeof(client_addr);

   server_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (server_fd < 0)
   {
      pgagroal_log_error("Cannot create socket");
      return NULL;
   }

   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = INADDR_ANY;
   server_addr.sin_port = htons(HTTP_PORT);

   if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
   {
      pgagroal_log_error("Bind failed");
      return NULL;
   }
   // TODO: ? connection requests will be queued
   if (listen(server_fd, 10) < 0)
   {
      pgagroal_log_error("Listen failed");
      return NULL;
   }

   printf("HTTP server is listening on port %d...\n", HTTP_PORT);

   while (1)
   {
      client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_fd < 0)
      {
         pgagroal_log_error("Accept failed");
         continue;
      }

      handle(client_fd, NULL, 1); // 1 indicates HTTP
   }

   close(server_fd);
   return NULL;
}

static void *https_thread_func(void *arg)
{
   int server_fd, client_fd;
   struct sockaddr_in server_addr, client_addr;
   socklen_t client_len = sizeof(client_addr);

   server_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (server_fd < 0)
   {
      pgagroal_log_error("Cannot create socket");
      return NULL;
   }

   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = INADDR_ANY;
   server_addr.sin_port = htons(HTTPS_PORT);

   if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
   {
      pgagroal_log_error("Bind failed");
      return NULL;
   }

   if (listen(server_fd, 10) < 0)
   {
      pgagroal_log_error("Listen failed");
      return NULL;
   }

   printf("HTTPS server is listening on port %d...\n", HTTPS_PORT);

   while (1)
   {
      client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_fd < 0)
      {
         pgagroal_log_error("Accept failed");
         continue;
      }

      SSL *ssl = SSL_new(ssl_ctx);
      SSL_set_fd(ssl, client_fd);

      if (SSL_accept(ssl) <= 0)
      {
         pgagroal_log_error("pgagroal_vault: ssl filed to accept");
      }
      else
      {
         handle(client_fd, ssl, 0); // 0 indicates HTTPS
      }

      // SSL_free(ssl);
   }

   close(server_fd);
   return NULL;
}

int main(int argc, char **argv)
{
   // int socket = -1;
   // SSL* s_ssl = NULL;
   int ret;
   // int exit_code = 0;
   char* configuration_path = NULL;
   char* host = NULL;
   char* port = NULL;
   char* username = NULL;
   char* password = NULL;
   // bool verbose = false;
   char* logfile = NULL;
   // bool do_free = true;
   int c;
   int option_index = 0;
   size_t size;
   // int32_t mode = FLUSH_IDLE;
   // char* database = NULL;
   // char un[MAX_USERNAME_LENGTH];
   // char* server = NULL;
   struct configuration* config = NULL;
   bool remote_connection = false;
   // long l_port;
   // char* config_key = NULL; /* key for a configuration setting */
   // char* config_value = NULL; /* value for a configuration setting */

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
         // case 'v':
         //    verbose = true;
         //    break;
         // case 'V':
         //    version();
         //    break;
         // case '?':
         //    usage();
         //    exit(1);
         //    break;
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

   // if (argc <= 1)
   // {
   //    usage();
   //    exit(1);
   // }

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

   // sock
   int reuse = 1;
   initialize_ssl();
   ssl_ctx = create_ssl_context();
   configure_ssl_context(ssl_ctx);

   client_socket = socket(AF_INET, SOCK_STREAM, 0);
   if (client_socket == -1)
   {
      perror("Socket creation error");
      exit(EXIT_FAILURE);
   }

   setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

   struct sockaddr_in serverAddress;
   serverAddress.sin_family = AF_INET;
   serverAddress.sin_port = htons(6789);
   serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

   if (connect(client_socket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1)
   {
      perror("Connection error");
      close(client_socket);
      exit(EXIT_FAILURE);
   }

   pid_t http_pid, https_pid;

   http_pid = fork();
   if (http_pid == 0)
   {
      // This is the HTTP child process
      http_thread_func(NULL);
      exit(0); // Terminate child process
   }
   else if (http_pid < 0)
   {
      // Handle fork failure
      perror("Failed to fork HTTP process");
      exit(EXIT_FAILURE);
   }

   https_pid = fork();
   if (https_pid == 0)
   {
      // This is the HTTPS child process
      https_thread_func(NULL);
      exit(0); // Terminate child process
   }
   else if (https_pid < 0)
   {
      // Handle fork failure
      perror("Failed to fork HTTPS process");
      exit(EXIT_FAILURE);
   }

   // Parent process waits for both HTTP and HTTPS child processes
   int status;
   waitpid(http_pid, &status, 0);
   waitpid(https_pid, &status, 0);
   SSL_CTX_free(ssl_ctx);
   return 0;
}
