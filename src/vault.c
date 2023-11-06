
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
static void handle_get_password_by_name(const char *method, struct KV *params, char *response, size_t response_size);
static void handle_info(const char *method, const char *contents, char *response, size_t response_size);
static void handle_default(char *response, size_t response_size);

static SSL_CTX *ssl_ctx;
static int client_socket;
static int known_fds[MAX_NUMBER_OF_CONNECTIONS];
static int* main_fds = NULL;
static int main_fds_length = -1;
static int unix_pgsql_socket = -1;

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
   char response[BUFFER_SIZE];
   if (strcmp(path, "/get_password_by_name") == 0)
   {
      handle_get_password_by_name(method, map, response, sizeof(response));
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

static void handle_get_password_by_name(const char *method, struct KV *params, char *response, size_t response_size)
{
   if (strcmp(method, "GET") == 0)
   {
      struct KV *kv;
      HASH_FIND_STR(params, "username", kv);
      struct message *msg;
      pgagroal_write_frontend_password_request(NULL, client_socket, kv->value);
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

int main(int arvc, char **argv)
{
   // Read conifg file just like pgagroal
   bool has_unix_socket = false;
   bool has_main_sockets = false;
   char* configuration_path = NULL;
   char* hba_path = NULL;
   char* limit_path = NULL;
   char* users_path = NULL;
   char* frontend_users_path = NULL;
   char* admins_path = NULL;
   char* superuser_path = NULL;
   void* tmp_shmem = NULL;
   size_t shmem_size;
   size_t tmp_size;
   struct configuration* config = NULL;
   int ret;
   bool conf_file_mandatory;
   char message[MISC_LENGTH];
#ifdef HAVE_LINUX
   int sds;
#endif

   if (getuid() == 0)
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      errx(1, "Using the root account is not allowed");
   }

   shmem_size = sizeof(struct configuration);
   if (pgagroal_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      errx(1, "Error in creating shared memory");
   }

   pgagroal_init_configuration(shmem);
   config = (struct configuration*)shmem;

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

   memcpy(&config->configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

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
   config = (struct configuration*)shmem;

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
