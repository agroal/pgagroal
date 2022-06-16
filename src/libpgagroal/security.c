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
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <pool.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <tracker.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

static int get_auth_type(struct message* msg, int* auth_type);
static int compare_auth_response(struct message* orig, struct message* response, int auth_type);

static int use_pooled_connection(SSL* c_ssl, int client_fd, int slot, char* username, char* database, int hba_method, SSL** server_ssl);
static int use_unpooled_connection(struct message* msg, SSL* c_ssl, int client_fd, int slot,
                                   char* username, int hba_method, SSL** server_ssl);
static int client_trust(SSL* c_ssl, int client_fd, char* username, char* password, int slot);
static int client_password(SSL* c_ssl, int client_fd, char* username, char* password, int slot);
static int client_md5(SSL* c_ssl, int client_fd, char* username, char* password, int slot);
static int client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot);
static int client_ok(SSL* c_ssl, int client_fd, int slot);
static int server_passthrough(struct message* msg, int auth_type, SSL* c_ssl, int client_fd, int slot);
static int server_authenticate(struct message* msg, int auth_type, char* username, char* password,
                               int slot, SSL* server_ssl);
static int server_trust(int slot, SSL* server_ssl);
static int server_password(char* username, char* password, int slot, SSL* server_ssl);
static int server_md5(char* username, char* password, int slot, SSL* server_ssl);
static int server_scram256(char* username, char* password, int slot, SSL* server_ssl);

static bool is_allowed(char* username, char* database, char* address, int* hba_method);
static bool is_allowed_username(char* username, char* entry);
static bool is_allowed_database(char* database, char* entry);
static bool is_allowed_address(char* address, char* entry);
static bool is_disabled(char* database);

static int   get_hba_method(int index);
static char* get_password(char* username);
static char* get_frontend_password(char* username);
static char* get_admin_password(char* username);
static int   get_salt(void* data, char** salt);

static int derive_key_iv(char* password, unsigned char* key, unsigned char* iv);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext);

static int sasl_prep(char* password, char** password_prep);
static int generate_nounce(char** nounce);
static int get_scram_attribute(char attribute, char* input, size_t size, char** value);
static int client_proof(char* password, char* salt, int salt_length, int iterations,
                        char* client_first_message_bare, size_t client_first_message_bare_length,
                        char* server_first_message, size_t server_first_message_length,
                        char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                        unsigned char** result, int* result_length);
static int verify_client_proof(char* stored_key, int stored_key_length,
                               char* client_proof, int client_proof_length,
                               char* salt, int salt_length, int iterations,
                               char* client_first_message_bare, size_t client_first_message_bare_length,
                               char* server_first_message, size_t server_first_message_length,
                               char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length);
static int  salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length);
static int  salted_password_key(unsigned char* salted_password, int salted_password_length, char* key,
                                unsigned char** result, int* result_length);
static int  stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length);
static int  generate_salt(char** salt, int* size);
static int  server_signature(char* password, char* salt, int salt_length, int iterations,
                             char* server_key, int server_key_length,
                             char* client_first_message_bare, size_t client_first_message_bare_length,
                             char* server_first_message, size_t server_first_message_length,
                             char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                             unsigned char** result, int* result_length);

static bool is_tls_user(char* username, char* database);
static int  create_ssl_ctx(bool client, SSL_CTX** ctx);
static int  create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl);
static int  create_ssl_server(SSL_CTX* ctx, int socket, SSL** ssl);
static int  establish_client_tls_connection(int server, int fd, SSL** ssl);
static int  create_client_tls_connection(int fd, SSL** ssl);

static int auth_query(SSL* c_ssl, int client_fd, int slot, char* username, char* database, int hba_method);
static int auth_query_get_connection(char* username, char* password, char* database, int* server_fd, SSL** server_ssl);
static int auth_query_server_md5(struct message* startup_response_msg, char* username, char* password, int socket, SSL* server_ssl);
static int auth_query_server_scram256(char* username, char* password, int socket, SSL* server_ssl);
static int auth_query_get_password(int socket, SSL* server_ssl, char* username, char* database, char** password);
static int auth_query_client_md5(SSL* c_ssl, int client_fd, char* username, char* hash, int slot);
static int auth_query_client_scram256(SSL* c_ssl, int client_fd, char* username, char* shadow, int slot);

int
pgagroal_authenticate(int client_fd, char* address, int* slot, SSL** client_ssl, SSL** server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int ret;
   int server = 0;
   int server_fd = -1;
   int hba_method;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* request_msg = NULL;
   int32_t request;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   SSL* c_ssl = NULL;

   config = (struct configuration*)shmem;

   *slot = -1;
   *client_ssl = NULL;
   *server_ssl = NULL;

   /* Receive client calls - at any point if client exits return AUTH_ERROR */
   status = pgagroal_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   request = pgagroal_get_request(msg);

   /* Cancel request: 80877102 */
   if (request == 80877102)
   {
      pgagroal_log_debug("Cancel request from client: %d", client_fd);

      /* We need to find the server for the connection */
      if (pgagroal_get_primary(&server))
      {
         pgagroal_log_error("pgagroal: No valid server available");
         pgagroal_write_connection_refused(NULL, client_fd);
         pgagroal_write_empty(NULL, client_fd);
         goto error;
      }

      if (config->servers[server].host[0] == '/')
      {
         char pgsql[MISC_LENGTH];

         memset(&pgsql, 0, sizeof(pgsql));
         snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
         ret = pgagroal_connect_unix_socket(config->servers[server].host, &pgsql[0], &server_fd);
      }
      else
      {
         ret = pgagroal_connect(config->servers[server].host, config->servers[server].port, &server_fd);
      }

      if (ret)
      {
         pgagroal_log_error("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
         goto error;
      }

      status = pgagroal_write_message(NULL, server_fd, msg);
      if (status != MESSAGE_STATUS_OK)
      {
         pgagroal_disconnect(server_fd);

         goto error;
      }
      pgagroal_free_message(msg);

      pgagroal_disconnect(server_fd);

      return AUTH_BAD_PASSWORD;
   }

   /* GSS request: 80877104 */
   if (request == 80877104)
   {
      pgagroal_log_debug("GSS request from client: %d", client_fd);
      status = pgagroal_write_notice(NULL, client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      status = pgagroal_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      request = pgagroal_get_request(msg);
   }

   /* SSL request: 80877103 */
   if (request == 80877103)
   {
      pgagroal_log_debug("SSL request from client: %d", client_fd);

      if (config->tls)
      {
         SSL_CTX* ctx = NULL;

         /* We are acting as a server against the client */
         if (create_ssl_ctx(false, &ctx))
         {
            goto error;
         }

         if (create_ssl_server(ctx, client_fd, &c_ssl))
         {
            pgagroal_log_debug("authenticate: connection error");
            pgagroal_write_connection_refused(NULL, client_fd);
            pgagroal_write_empty(NULL, client_fd);
            goto error;
         }

         *client_ssl = c_ssl;

         /* Switch to TLS mode */
         status = pgagroal_write_tls(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = SSL_accept(c_ssl);
         if (status != 1)
         {
            unsigned long err;

            err = ERR_get_error();
            pgagroal_log_error("SSL failed: %s", ERR_reason_error_string(err));
            goto error;
         }

         status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgagroal_get_request(msg);
      }
      else
      {
         status = pgagroal_write_notice(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = pgagroal_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgagroal_get_request(msg);
      }
   }

   /* 196608 -> Ok */
   if (request == 196608)
   {
      request_msg = pgagroal_copy_message(msg);

      /* Extract parameters: username / database */
      pgagroal_log_trace("authenticate: username/database (%d)", client_fd);
      pgagroal_extract_username_database(request_msg, &username, &database, &appname);

      /* TLS scenario */
      if (is_tls_user(username, database) && c_ssl == NULL)
      {
         pgagroal_log_debug("authenticate: tls: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Verify client against pgagroal_hba.conf */
      if (!is_allowed(username, database, address, &hba_method))
      {
         /* User not allowed */
         pgagroal_log_debug("authenticate: not allowed: %s / %s / %s", username, database, address);
         pgagroal_write_no_hba_entry(c_ssl, client_fd, username, database, address);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Reject scenario */
      if (hba_method == SECURITY_REJECT)
      {
         pgagroal_log_debug("authenticate: reject: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Gracefully scenario */
      if (config->gracefully)
      {
         pgagroal_log_debug("authenticate: gracefully: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Disabled scenario */
      if (is_disabled(database))
      {
         pgagroal_log_debug("authenticate: disabled: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Get connection */
      pgagroal_tracking_event_basic(TRACKER_AUTHENTICATE, username, database);
      ret = pgagroal_get_connection(username, database, true, false, slot, server_ssl);
      if (ret != 0)
      {
         if (ret == 1)
         {
            /* Pool full */
            pgagroal_log_debug("authenticate: pool is full");
            pgagroal_write_pool_full(c_ssl, client_fd);
            pgagroal_write_empty(c_ssl, client_fd);
            goto bad_password;
         }
         else
         {
            /* Other error */
            pgagroal_log_debug("authenticate: connection error");
            pgagroal_write_connection_refused(c_ssl, client_fd);
            pgagroal_write_empty(c_ssl, client_fd);
            goto error;
         }
      }

      /* Set the application_name on the connection */
      if (appname != NULL)
      {
         memset(&config->connections[*slot].appname, 0, MAX_APPLICATION_NAME);
         memcpy(&config->connections[*slot].appname, appname, strlen(appname));
      }

      if (config->connections[*slot].has_security != SECURITY_INVALID)
      {
         pgagroal_log_debug("authenticate: getting pooled connection");
         pgagroal_free_message(msg);

         ret = use_pooled_connection(c_ssl, client_fd, *slot, username, database, hba_method, server_ssl);
         if (ret == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (ret == AUTH_ERROR)
         {
            goto error;
         }

         pgagroal_log_debug("authenticate: got pooled connection (%d)", *slot);
      }
      else
      {
         pgagroal_log_debug("authenticate: creating pooled connection");

         ret = use_unpooled_connection(request_msg, c_ssl, client_fd, *slot, username, hba_method, server_ssl);
         if (ret == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (ret == AUTH_ERROR)
         {
            goto error;
         }

         pgagroal_log_debug("authenticate: created pooled connection (%d)", *slot);
      }

      pgagroal_free_copy_message(request_msg);
      free(username);
      free(database);
      free(appname);

      pgagroal_prometheus_auth_user_success();

      pgagroal_log_debug("authenticate: SUCCESS");
      return AUTH_SUCCESS;
   }
   else if (request == -1)
   {
      goto error;
   }
   else
   {
      pgagroal_log_debug("authenticate: old version: %d (%s)", request, address);
      pgagroal_write_connection_refused_old(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto bad_password;
   }

bad_password:
   pgagroal_free_message(msg);
   pgagroal_free_copy_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgagroal_prometheus_auth_user_bad_password();

   pgagroal_log_debug("authenticate: BAD_PASSWORD");
   return AUTH_BAD_PASSWORD;

error:
   pgagroal_free_message(msg);
   pgagroal_free_copy_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgagroal_prometheus_auth_user_error();

   pgagroal_log_debug("authenticate: ERROR");
   return AUTH_ERROR;
}

int
pgagroal_prefill_auth(char* username, char* password, char* database, int* slot, SSL** server_ssl)
{
   int server_fd = -1;
   int auth_type = -1;
   signed char server_state;
   struct configuration* config = NULL;
   struct message* startup_msg = NULL;
   struct message* msg = NULL;
   int ret = -1;
   int status = -1;

   config = (struct configuration*)shmem;

   *slot = -1;
   *server_ssl = NULL;

   /* Get connection */
   pgagroal_tracking_event_basic(TRACKER_PREFILL, username, database);
   ret = pgagroal_get_connection(username, database, false, false, slot, server_ssl);
   if (ret != 0)
   {
      goto error;
   }
   server_fd = config->connections[*slot].fd;

   status = pgagroal_create_startup_message(username, database, &startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(*server_ssl, server_fd, startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(*server_ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_auth_type(msg, &auth_type);
   pgagroal_log_trace("prefill_auth: auth type %d", auth_type);

   /* Supported security models: */
   /*   trust (0) */
   /*   password (3) */
   /*   md5 (5) */
   /*   scram256 (10) */
   if (auth_type == -1)
   {
      goto error;
   }
   else if (auth_type != SECURITY_TRUST && auth_type != SECURITY_PASSWORD && auth_type != SECURITY_MD5 && auth_type != SECURITY_SCRAM256)
   {
      goto error;
   }

   if (server_authenticate(msg, auth_type, username, password, *slot, *server_ssl))
   {
      goto error;
   }

   server_state = atomic_load(&config->servers[config->connections[*slot].server].state);
   if (server_state == SERVER_NOTINIT || server_state == SERVER_NOTINIT_PRIMARY)
   {
      pgagroal_log_debug("Verify server mode: %d", config->connections[*slot].server);
      pgagroal_update_server_state(*slot, server_fd, *server_ssl);
      pgagroal_server_status();
   }

   pgagroal_log_trace("prefill_auth: has_security %d", config->connections[*slot].has_security);
   pgagroal_log_debug("prefill_auth: SUCCESS");

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_message(msg);

   return AUTH_SUCCESS;

error:

   pgagroal_log_debug("prefill_auth: ERROR");

   if (*slot != -1)
   {
      pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, *slot);
      pgagroal_kill_connection(*slot, *server_ssl);
   }

   *slot = -1;
   *server_ssl = NULL;

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_message(msg);

   return AUTH_ERROR;
}

int
pgagroal_remote_management_auth(int client_fd, char* address, SSL** client_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int hba_method;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* request_msg = NULL;
   int32_t request;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   char* password = NULL;
   SSL* c_ssl = NULL;

   config = (struct configuration*)shmem;

   *client_ssl = NULL;

   /* Receive client calls - at any point if client exits return AUTH_ERROR */
   status = pgagroal_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   request = pgagroal_get_request(msg);

   /* SSL request: 80877103 */
   if (request == 80877103)
   {
      pgagroal_log_debug("SSL request from client: %d", client_fd);

      if (config->tls)
      {
         SSL_CTX* ctx = NULL;

         /* We are acting as a server against the client */
         if (create_ssl_ctx(false, &ctx))
         {
            goto error;
         }

         if (create_ssl_server(ctx, client_fd, &c_ssl))
         {
            goto error;
         }

         *client_ssl = c_ssl;

         /* Switch to TLS mode */
         status = pgagroal_write_tls(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = SSL_accept(c_ssl);
         if (status != 1)
         {
            unsigned long err;

            err = ERR_get_error();
            pgagroal_log_error("SSL failed: %s", ERR_reason_error_string(err));
            goto error;
         }

         status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgagroal_get_request(msg);
      }
      else
      {
         status = pgagroal_write_notice(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = pgagroal_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgagroal_get_request(msg);
      }
   }

   /* 196608 -> Ok */
   if (request == 196608)
   {
      request_msg = pgagroal_copy_message(msg);

      /* Extract parameters: username / database */
      pgagroal_log_trace("remote_management_auth: username/database (%d)", client_fd);
      pgagroal_extract_username_database(request_msg, &username, &database, &appname);

      /* Must be admin database */
      if (strcmp("admin", database) != 0)
      {
         pgagroal_log_debug("remote_management_auth: admin: %s / %s", username, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* TLS scenario */
      if (is_tls_user(username, "admin") && c_ssl == NULL)
      {
         pgagroal_log_debug("remote_management_auth: tls: %s / admin / %s", username, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Verify client against pgagroal_hba.conf */
      if (!is_allowed(username, "admin", address, &hba_method))
      {
         /* User not allowed */
         pgagroal_log_debug("remote_management_auth: not allowed: %s / admin / %s", username, address);
         pgagroal_write_no_hba_entry(c_ssl, client_fd, username, "admin", address);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Reject scenario */
      if (hba_method == SECURITY_REJECT)
      {
         pgagroal_log_debug("remote_management_auth: reject: %s / admin / %s", username, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      password = get_admin_password(username);
      if (password == NULL)
      {
         pgagroal_log_debug("remote_management_auth: password: %s / admin / %s", username, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      status = client_scram256(c_ssl, client_fd, username, password, -1);
      if (status == AUTH_BAD_PASSWORD)
      {
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }
      else if (status == AUTH_ERROR)
      {
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto error;
      }

      status = pgagroal_write_auth_success(c_ssl, client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      pgagroal_free_copy_message(request_msg);
      free(username);
      free(database);
      free(appname);

      pgagroal_log_debug("remote_management_auth: SUCCESS");
      return AUTH_SUCCESS;
   }
   else if (request == -1)
   {
      goto error;
   }
   else
   {
      pgagroal_log_debug("remote_management_auth: old version: %d (%s)", request, address);
      pgagroal_write_connection_refused_old(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto bad_password;
   }

bad_password:
   pgagroal_free_message(msg);
   pgagroal_free_copy_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgagroal_log_debug("remote_management_auth: BAD_PASSWORD");
   return AUTH_BAD_PASSWORD;

error:
   pgagroal_free_message(msg);
   pgagroal_free_copy_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgagroal_log_debug("remote_management_auth: ERROR");
   return AUTH_ERROR;
}

int
pgagroal_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   SSL* ssl = NULL;
   char key_file[MISC_LENGTH];
   char cert_file[MISC_LENGTH];
   char root_file[MISC_LENGTH];
   struct stat st = {0};
   char* salt = NULL;
   int salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   int proof_length;
   char* proof_base = NULL;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   int server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length;
   struct message* sslrequest_msg = NULL;
   struct message* startup_msg = NULL;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;

   pgagroal_memory_size(DEFAULT_BUFFER_SIZE);

   if (pgagroal_get_home_directory() == NULL)
   {
      goto error;
   }

   memset(&key_file, 0, sizeof(key_file));
   snprintf(&key_file[0], sizeof(key_file), "%s/.pgagroal/pgagroal.key", pgagroal_get_home_directory());

   memset(&cert_file, 0, sizeof(cert_file));
   snprintf(&cert_file[0], sizeof(cert_file), "%s/.pgagroal/pgagroal.crt", pgagroal_get_home_directory());

   memset(&root_file, 0, sizeof(root_file));
   snprintf(&root_file[0], sizeof(root_file), "%s/.pgagroal/root.crt", pgagroal_get_home_directory());

   if (stat(&key_file[0], &st) == 0)
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         if (stat(&cert_file[0], &st) == 0)
         {
            if (S_ISREG(st.st_mode))
            {
               SSL_CTX* ctx = NULL;

               status = pgagroal_create_ssl_message(&sslrequest_msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               status = pgagroal_write_message(NULL, server_fd, sslrequest_msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               status = pgagroal_read_block_message(NULL, server_fd, &msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               if (msg->kind == 'S')
               {
                  if (create_ssl_ctx(true, &ctx))
                  {
                     goto error;
                  }

                  if (stat(&root_file[0], &st) == -1)
                  {
                     memset(&root_file, 0, sizeof(root_file));
                  }

                  if (create_ssl_client(ctx, &key_file[0], &cert_file[0], &root_file[0], server_fd, &ssl))
                  {
                     goto error;
                  }

                  *s_ssl = ssl;

                  do
                  {
                     status = SSL_connect(ssl);

                     if (status != 1)
                     {
                        int err = SSL_get_error(ssl, status);
                        switch (err)
                        {
                           case SSL_ERROR_ZERO_RETURN:
                           case SSL_ERROR_WANT_READ:
                           case SSL_ERROR_WANT_WRITE:
                           case SSL_ERROR_WANT_CONNECT:
                           case SSL_ERROR_WANT_ACCEPT:
                           case SSL_ERROR_WANT_X509_LOOKUP:
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
                           case SSL_ERROR_WANT_ASYNC:
                           case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
                           case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
                              break;
                           case SSL_ERROR_SYSCALL:
                              pgagroal_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), server_fd);
                              errno = 0;
                              goto error;
                              break;
                           case SSL_ERROR_SSL:
                              pgagroal_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), server_fd);
                              pgagroal_log_error("%s", ERR_error_string(err, NULL));
                              pgagroal_log_error("%s", ERR_lib_error_string(err));
                              pgagroal_log_error("%s", ERR_reason_error_string(err));
                              errno = 0;
                              goto error;
                              break;
                        }
                        ERR_clear_error();
                     }
                  }
                  while (status != 1);
               }
            }
         }
      }
   }

   status = pgagroal_create_startup_message(username, "admin", &startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(ssl, server_fd, startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (msg->kind != 'R')
   {
      goto error;
   }

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgagroal_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(ssl, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgagroal_copy_message(msg);

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      goto error;
   }

   pgagroal_base64_decode(base64_salt, strlen(base64_salt), &salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = sasl_response->data + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = sasl_continue->data + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, sasl_response->length - 26,
                    server_first_message, sasl_continue->length - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgagroal_base64_encode((char*)proof, proof_length, &proof_base);

   status = pgagroal_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(ssl, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (pgagroal_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgagroal_base64_decode(base64_server_signature, sasl_final->length - 11, &server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, sasl_response->length - 26,
                        server_first_message, sasl_continue->length - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   if (msg->length == 55)
   {
      status = pgagroal_read_block_message(ssl, server_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
   }

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_message(msg);
   pgagroal_free_copy_message(sslrequest_msg);
   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   pgagroal_memory_destroy();

   return AUTH_SUCCESS;

bad_password:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_message(msg);
   pgagroal_free_copy_message(sslrequest_msg);
   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   pgagroal_memory_destroy();

   return AUTH_BAD_PASSWORD;

error:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_message(msg);
   pgagroal_free_copy_message(sslrequest_msg);
   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   pgagroal_memory_destroy();

   return AUTH_ERROR;
}

static int
get_auth_type(struct message* msg, int* auth_type)
{
   int32_t length;
   int32_t type = -1;
   int offset;

   *auth_type = -1;

   if (msg->kind != 'R')
   {
      return 1;
   }

   length = pgagroal_read_int32(msg->data + 1);
   type = pgagroal_read_int32(msg->data + 5);
   offset = 9;

   if (type == 0 && msg->length > 8)
   {
      if ('E' == pgagroal_read_byte(msg->data + 9))
      {
         return 0;
      }
   }

   switch (type)
   {
      case 0:
         pgagroal_log_trace("Backend: R - Success");
         break;
      case 2:
         pgagroal_log_trace("Backend: R - KerberosV5");
         break;
      case 3:
         pgagroal_log_trace("Backend: R - CleartextPassword");
         break;
      case 5:
         pgagroal_log_trace("Backend: R - MD5Password");
         pgagroal_log_trace("             Salt %02hhx%02hhx%02hhx%02hhx",
                            (signed char)(pgagroal_read_byte(msg->data + 9) & 0xFF),
                            (signed char)(pgagroal_read_byte(msg->data + 10) & 0xFF),
                            (signed char)(pgagroal_read_byte(msg->data + 11) & 0xFF),
                            (signed char)(pgagroal_read_byte(msg->data + 12) & 0xFF));
         break;
      case 6:
         pgagroal_log_trace("Backend: R - SCMCredential");
         break;
      case 7:
         pgagroal_log_trace("Backend: R - GSS");
         break;
      case 8:
         pgagroal_log_trace("Backend: R - GSSContinue");
         break;
      case 9:
         pgagroal_log_trace("Backend: R - SSPI");
         break;
      case 10:
         pgagroal_log_trace("Backend: R - SASL");
         while (offset < length - 8)
         {
            char* mechanism = pgagroal_read_string(msg->data + offset);
            pgagroal_log_trace("             %s", mechanism);
            offset += strlen(mechanism) + 1;
         }
         break;
      case 11:
         pgagroal_log_trace("Backend: R - SASLContinue");
         break;
      case 12:
         pgagroal_log_trace("Backend: R - SASLFinal");
         offset += length - 8;

         if (offset < msg->length)
         {
            signed char peek = pgagroal_read_byte(msg->data + offset);
            switch (peek)
            {
               case 'R':
                  type = pgagroal_read_int32(msg->data + offset + 5);
                  break;
               default:
                  break;
            }
         }

         break;
      default:
         break;
   }

   *auth_type = type;

   return 0;
}

static int
compare_auth_response(struct message* orig, struct message* response, int auth_type)
{
   switch (auth_type)
   {
      case 3:
      case 5:
         return strcmp(pgagroal_read_string(orig->data + 5), pgagroal_read_string(response->data + 5));
         break;
      case 10:
         return memcmp(orig->data, response->data, orig->length);
         break;
      default:
         break;
   }

   return 1;
}

static int
use_pooled_connection(SSL* c_ssl, int client_fd, int slot, char* username, char* database, int hba_method, SSL** server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   struct configuration* config = NULL;
   struct message* auth_msg = NULL;
   struct message* msg = NULL;
   char* password = NULL;

   config = (struct configuration*)shmem;

   password = get_frontend_password(username);
   if (password == NULL)
   {
      password = get_password(username);
   }

   if (hba_method == SECURITY_ALL)
   {
      hba_method = config->connections[slot].has_security;
   }

   if (config->authquery)
   {
      status = auth_query(c_ssl, client_fd, slot, username, database, hba_method);
      if (status == AUTH_BAD_PASSWORD)
      {
         goto bad_password;
      }
      else if (status == AUTH_ERROR)
      {
         goto error;
      }
   }
   else if (password == NULL)
   {
      /* We can only deal with SECURITY_TRUST, SECURITY_PASSWORD and SECURITY_MD5 */
      pgagroal_create_message(&config->connections[slot].security_messages[0],
                              config->connections[slot].security_lengths[0],
                              &auth_msg);

      status = pgagroal_write_message(c_ssl, client_fd, auth_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_copy_message(auth_msg);
      auth_msg = NULL;

      /* Password or MD5 */
      if (config->connections[slot].has_security != SECURITY_TRUST)
      {
         status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         pgagroal_create_message(&config->connections[slot].security_messages[1],
                                 config->connections[slot].security_lengths[1],
                                 &auth_msg);

         if (compare_auth_response(auth_msg, msg, config->connections[slot].has_security))
         {
            pgagroal_write_bad_password(c_ssl, client_fd, username);
            pgagroal_write_empty(c_ssl, client_fd);

            goto error;
         }

         pgagroal_free_copy_message(auth_msg);
         auth_msg = NULL;

         pgagroal_create_message(&config->connections[slot].security_messages[2],
                                 config->connections[slot].security_lengths[2],
                                 &auth_msg);

         status = pgagroal_write_message(c_ssl, client_fd, auth_msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_copy_message(auth_msg);
         auth_msg = NULL;
      }
   }
   else
   {
      /* We have a password */

      if (hba_method == SECURITY_TRUST)
      {
         /* R/0 */
         client_trust(c_ssl, client_fd, username, password, slot);
      }
      else if (hba_method == SECURITY_PASSWORD)
      {
         /* R/3 */
         status = client_password(c_ssl, client_fd, username, password, slot);
         if (status == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (status == AUTH_ERROR)
         {
            goto error;
         }
      }
      else if (hba_method == SECURITY_MD5)
      {
         /* R/5 */
         status = client_md5(c_ssl, client_fd, username, password, slot);
         if (status == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (status == AUTH_ERROR)
         {
            goto error;
         }
      }
      else if (hba_method == SECURITY_SCRAM256)
      {
         /* R/10 */
         status = client_scram256(c_ssl, client_fd, username, password, slot);
         if (status == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (status == AUTH_ERROR)
         {
            goto error;
         }
      }
      else
      {
         goto error;
      }

      if (client_ok(c_ssl, client_fd, slot))
      {
         goto error;
      }
   }

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_trace("use_pooled_connection: bad password for slot %d", slot);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_log_trace("use_pooled_connection: failed for slot %d", slot);

   return AUTH_ERROR;
}

static int
use_unpooled_connection(struct message* request_msg, SSL* c_ssl, int client_fd, int slot,
                        char* username, int hba_method, SSL** server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int server_fd;
   int auth_type = -1;
   char* password;
   signed char server_state;
   struct message* msg = NULL;
   struct message* auth_msg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   password = get_frontend_password(username);
   if (password == NULL)
   {
      password = get_password(username);
   }

   /* Disallow unknown users */
   if (password == NULL && !config->allow_unknown_users)
   {
      pgagroal_log_debug("reject: %s", username);
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }

   /* TLS support */
   establish_client_tls_connection(config->connections[slot].server, server_fd, server_ssl);

   /* Send auth request to PostgreSQL */
   pgagroal_log_trace("authenticate: client auth request (%d)", client_fd);
   status = pgagroal_write_message(*server_ssl, server_fd, request_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(msg);

   /* Keep response, and send response to client */
   pgagroal_log_trace("authenticate: server auth request (%d)", server_fd);
   status = pgagroal_read_block_message(*server_ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_auth_type(msg, &auth_type);
   pgagroal_log_trace("authenticate: auth type %d", auth_type);

   /* Supported security models: */
   /*   trust (0) */
   /*   password (3) */
   /*   md5 (5) */
   /*   scram-sha-256 (10) */
   if (auth_type == -1)
   {
      pgagroal_write_message(c_ssl, client_fd, msg);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }
   else if (auth_type != SECURITY_TRUST && auth_type != SECURITY_PASSWORD && auth_type != SECURITY_MD5 && auth_type != SECURITY_SCRAM256)
   {
      pgagroal_log_info("Unsupported security model: %d", auth_type);
      pgagroal_write_unsupported_security_model(c_ssl, client_fd, username);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }

   if (password == NULL)
   {
      if (server_passthrough(msg, auth_type, c_ssl, client_fd, slot))
      {
         goto error;
      }
   }
   else
   {
      if (hba_method == SECURITY_ALL)
      {
         hba_method = auth_type;
      }

      auth_msg = pgagroal_copy_message(msg);

      if (hba_method == SECURITY_TRUST)
      {
         /* R/0 */
         client_trust(c_ssl, client_fd, username, password, slot);
      }
      else if (hba_method == SECURITY_PASSWORD)
      {
         /* R/3 */
         status = client_password(c_ssl, client_fd, username, password, slot);
         if (status == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (status == AUTH_ERROR)
         {
            goto error;
         }
      }
      else if (hba_method == SECURITY_MD5)
      {
         /* R/5 */
         status = client_md5(c_ssl, client_fd, username, password, slot);
         if (status == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (status == AUTH_ERROR)
         {
            goto error;
         }
      }
      else if (hba_method == SECURITY_SCRAM256)
      {
         /* R/10 */
         status = client_scram256(c_ssl, client_fd, username, password, slot);
         if (status == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (status == AUTH_ERROR)
         {
            goto error;
         }
      }
      else
      {
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);

         goto error;
      }

      if (server_authenticate(auth_msg, auth_type, username, get_password(username), slot, *server_ssl))
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            pgagroal_write_connection_refused(c_ssl, client_fd);
            pgagroal_write_empty(c_ssl, client_fd);
         }

         goto error;
      }

      if (client_ok(c_ssl, client_fd, slot))
      {
         goto error;
      }
   }

   server_state = atomic_load(&config->servers[config->connections[slot].server].state);
   if (server_state == SERVER_NOTINIT || server_state == SERVER_NOTINIT_PRIMARY)
   {
      pgagroal_log_debug("Verify server mode: %d", config->connections[slot].server);
      pgagroal_update_server_state(slot, server_fd, *server_ssl);
      pgagroal_server_status();
   }

   pgagroal_log_trace("authenticate: has_security %d", config->connections[slot].has_security);

   pgagroal_free_copy_message(auth_msg);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_free_copy_message(auth_msg);

   if (pgagroal_socket_isvalid(client_fd))
   {
      pgagroal_write_bad_password(c_ssl, client_fd, username);
      if (hba_method == SECURITY_SCRAM256)
      {
         pgagroal_write_empty(c_ssl, client_fd);
      }
   }

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_free_copy_message(auth_msg);

   pgagroal_log_trace("use_unpooled_connection: failed for slot %d", slot);

   return AUTH_ERROR;
}

static int
client_trust(SSL* c_ssl, int client_fd, char* username, char* password, int slot)
{
   pgagroal_log_debug("client_trust %d %d", client_fd, slot);

   return AUTH_SUCCESS;
}

static int
client_password(SSL* c_ssl, int client_fd, char* username, char* password, int slot)
{
   int status;
   time_t start_time;
   bool non_blocking;
   struct configuration* config;
   struct message* msg = NULL;

   pgagroal_log_debug("client_password %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgagroal_write_auth_password(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgagroal_socket_is_nonblocking(client_fd);
   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000L;
            nanosleep(&ts, NULL);

            goto retry;
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgagroal_socket_nonblocking(client_fd, false);
   }

   if (strcmp(pgagroal_read_string(msg->data + 5), password))
   {
      pgagroal_write_bad_password(c_ssl, client_fd, username);

      goto bad_password;
   }

   pgagroal_free_message(msg);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_free_message(msg);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_free_message(msg);

   return AUTH_ERROR;
}

static int
client_md5(SSL* c_ssl, int client_fd, char* username, char* password, int slot)
{
   int status;
   char salt[4];
   time_t start_time;
   bool non_blocking;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   struct configuration* config;
   struct message* msg = NULL;

   pgagroal_log_debug("client_md5 %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   salt[0] = (char)(random() & 0xFF);
   salt[1] = (char)(random() & 0xFF);
   salt[2] = (char)(random() & 0xFF);
   salt[3] = (char)(random() & 0xFF);

   status = pgagroal_write_auth_md5(c_ssl, client_fd, salt);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgagroal_socket_is_nonblocking(client_fd);
   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000L;
            nanosleep(&ts, NULL);

            goto retry;
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgagroal_socket_nonblocking(client_fd, false);
   }

   size = strlen(username) + strlen(password) + 1;
   pwdusr = malloc(size);
   memset(pwdusr, 0, size);

   snprintf(pwdusr, size, "%s%s", password, username);

   if (pgagroal_md5(pwdusr, strlen(pwdusr), &shadow))
   {
      goto error;
   }

   md5_req = malloc(36);
   memset(md5_req, 0, 36);
   memcpy(md5_req, shadow, 32);
   memcpy(md5_req + 32, &salt[0], 4);

   if (pgagroal_md5(md5_req, 36, &md5))
   {
      goto error;
   }

   if (strcmp(pgagroal_read_string(msg->data + 8), md5))
   {
      pgagroal_write_bad_password(c_ssl, client_fd, username);

      goto bad_password;
   }

   pgagroal_free_message(msg);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_free_message(msg);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_free_message(msg);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);

   return AUTH_ERROR;
}

static int
client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot)
{
   int status;
   time_t start_time;
   bool non_blocking;
   char* password_prep = NULL;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char* client_final_message_without_proof = NULL;
   char* client_nounce = NULL;
   char* server_nounce = NULL;
   char* salt = NULL;
   int salt_length = 0;
   char* base64_salt = NULL;
   char* base64_client_proof = NULL;
   char* client_proof_received = NULL;
   int client_proof_received_length = 0;
   unsigned char* client_proof_calc = NULL;
   int client_proof_calc_length = 0;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length = 0;
   char* base64_server_signature_calc = NULL;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_final = NULL;

   pgagroal_log_debug("client_scram256 %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgagroal_write_auth_scram256(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgagroal_socket_is_nonblocking(client_fd);
   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000L;
            nanosleep(&ts, NULL);

            goto retry;
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgagroal_socket_nonblocking(client_fd, false);
   }

   client_first_message_bare = malloc(msg->length - 25);
   memset(client_first_message_bare, 0, msg->length - 25);
   memcpy(client_first_message_bare, msg->data + 26, msg->length - 26);

   get_scram_attribute('r', (char*)msg->data + 26, msg->length - 26, &client_nounce);
   generate_nounce(&server_nounce);
   generate_salt(&salt, &salt_length);
   pgagroal_base64_encode(salt, salt_length, &base64_salt);

   server_first_message = malloc(89);
   memset(server_first_message, 0, 89);
   snprintf(server_first_message, 89, "r=%s%s,s=%s,i=4096", client_nounce, server_nounce, base64_salt);

   status = pgagroal_create_auth_scram256_continue(client_nounce, server_nounce, base64_salt, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgagroal_copy_message(msg);

   pgagroal_free_copy_message(msg);
   msg = NULL;

   status = pgagroal_write_message(c_ssl, client_fd, sasl_continue);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_scram_attribute('p', (char*)msg->data + 5, msg->length - 5, &base64_client_proof);
   pgagroal_base64_decode(base64_client_proof, strlen(base64_client_proof), &client_proof_received, &client_proof_received_length);

   client_final_message_without_proof = malloc(58);
   memset(client_final_message_without_proof, 0, 58);
   memcpy(client_final_message_without_proof, msg->data + 5, 57);

   sasl_prep(password, &password_prep);

   if (client_proof(password_prep, salt, salt_length, 4096,
                    client_first_message_bare, strlen(client_first_message_bare),
                    server_first_message, strlen(server_first_message),
                    client_final_message_without_proof, strlen(client_final_message_without_proof),
                    &client_proof_calc, &client_proof_calc_length))
   {
      goto error;
   }

   if (client_proof_received_length != client_proof_calc_length ||
       memcmp(client_proof_received, client_proof_calc, client_proof_calc_length) != 0)
   {
      goto bad_password;
   }

   if (server_signature(password_prep, salt, salt_length, 4096,
                        NULL, 0,
                        client_first_message_bare, strlen(client_first_message_bare),
                        server_first_message, strlen(server_first_message),
                        client_final_message_without_proof, strlen(client_final_message_without_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   pgagroal_base64_encode((char*)server_signature_calc, server_signature_calc_length, &base64_server_signature_calc);

   status = pgagroal_create_auth_scram256_final(base64_server_signature_calc, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_final = pgagroal_copy_message(msg);

   pgagroal_free_copy_message(msg);
   msg = NULL;

   status = pgagroal_write_message(c_ssl, client_fd, sasl_final);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgagroal_log_debug("client_scram256 done");

   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:
   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:
   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_ERROR;
}

static int
client_ok(SSL* c_ssl, int client_fd, int slot)
{
   int status;
   size_t size;
   char* data;
   struct message msg;
   struct configuration* config;

   data = NULL;
   memset(&msg, 0, sizeof(msg));

   config = (struct configuration*)shmem;

   if (config->connections[slot].has_security == SECURITY_TRUST)
   {
      size = config->connections[slot].security_lengths[0];
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[0], size);
   }
   else if (config->connections[slot].has_security == SECURITY_PASSWORD || config->connections[slot].has_security == SECURITY_MD5)
   {
      size = config->connections[slot].security_lengths[2];
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[2], size);
   }
   else if (config->connections[slot].has_security == SECURITY_SCRAM256)
   {
      size = config->connections[slot].security_lengths[4] - 55;
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[4] + 55, size);
   }
   else
   {
      goto error;
   }

   msg.kind = 'R';
   msg.length = size;
   msg.data = data;

   status = pgagroal_write_message(c_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);

   return 0;

error:

   free(data);

   return 1;
}

static int
server_passthrough(struct message* msg, int auth_type, SSL* c_ssl, int client_fd, int slot)
{
   int status = MESSAGE_STATUS_ERROR;
   int server_fd;
   int auth_index = 0;
   int auth_response = -1;
   struct message* smsg = NULL;
   struct message* kmsg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   pgagroal_log_trace("server_passthrough %d %d", auth_type, slot);

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = msg->length;
   memcpy(&config->connections[slot].security_messages[auth_index], msg->data, msg->length);
   auth_index++;

   status = pgagroal_write_message(c_ssl, client_fd, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(msg);

   /* Non-trust clients */
   if (auth_type != SECURITY_TRUST)
   {
      /* Receive client response, keep it, and send it to PostgreSQL */
      status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (msg->length > SECURITY_BUFFER_SIZE)
      {
         pgagroal_log_error("Security message too large: %ld", msg->length);
         goto error;
      }

      config->connections[slot].security_lengths[auth_index] = msg->length;
      memcpy(&config->connections[slot].security_messages[auth_index], msg->data, msg->length);
      auth_index++;

      status = pgagroal_write_message(NULL, server_fd, msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      status = pgagroal_read_block_message(NULL, server_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (auth_type == SECURITY_SCRAM256)
      {
         if (msg->length > SECURITY_BUFFER_SIZE)
         {
            pgagroal_log_error("Security message too large: %ld", msg->length);
            goto error;
         }

         config->connections[slot].security_lengths[auth_index] = msg->length;
         memcpy(&config->connections[slot].security_messages[auth_index], msg->data, msg->length);
         auth_index++;

         status = pgagroal_write_message(c_ssl, client_fd, msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         if (msg->length > SECURITY_BUFFER_SIZE)
         {
            pgagroal_log_error("Security message too large: %ld", msg->length);
            goto error;
         }

         config->connections[slot].security_lengths[auth_index] = msg->length;
         memcpy(&config->connections[slot].security_messages[auth_index], msg->data, msg->length);
         auth_index++;

         status = pgagroal_write_message(NULL, server_fd, msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = pgagroal_read_block_message(NULL, server_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }

      /* Ok: Keep the response, send it to the client, and exit authenticate() */
      get_auth_type(msg, &auth_response);
      pgagroal_log_trace("authenticate: auth response %d", auth_response);

      if (auth_response == 0)
      {
         if (msg->length > SECURITY_BUFFER_SIZE)
         {
            pgagroal_log_error("Security message too large: %ld", msg->length);
            goto error;
         }

         config->connections[slot].security_lengths[auth_index] = msg->length;
         memcpy(&config->connections[slot].security_messages[auth_index], msg->data, msg->length);

         config->connections[slot].has_security = auth_type;
      }

      status = pgagroal_write_message(c_ssl, client_fd, msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      if (auth_response != 0)
      {
         goto error;
      }
   }
   else
   {
      /* Trust */
      config->connections[slot].has_security = SECURITY_TRUST;
   }

   if (config->connections[slot].has_security == SECURITY_TRUST)
   {
      pgagroal_create_message(&config->connections[slot].security_messages[0],
                              config->connections[slot].security_lengths[0],
                              &smsg);
   }
   else if (config->connections[slot].has_security == SECURITY_PASSWORD || config->connections[slot].has_security == SECURITY_MD5)
   {
      pgagroal_create_message(&config->connections[slot].security_messages[2],
                              config->connections[slot].security_lengths[2],
                              &smsg);
   }
   else if (config->connections[slot].has_security == SECURITY_SCRAM256)
   {
      pgagroal_create_message(&config->connections[slot].security_messages[4],
                              config->connections[slot].security_lengths[4],
                              &smsg);
   }

   if (smsg != NULL)
   {
      pgagroal_extract_message('K', smsg, &kmsg);

      if (kmsg != NULL)
      {
         config->connections[slot].backend_pid = pgagroal_read_int32(kmsg->data + 5);
         config->connections[slot].backend_secret = pgagroal_read_int32(kmsg->data + 9);
      }
   }

   pgagroal_free_copy_message(smsg);
   pgagroal_free_copy_message(kmsg);

   return 0;

error:

   pgagroal_free_copy_message(smsg);
   pgagroal_free_copy_message(kmsg);

   return 1;
}

static int
server_authenticate(struct message* msg, int auth_type, char* username, char* password, int slot, SSL* server_ssl)
{
   int ret = AUTH_ERROR;
   struct message* smsg = NULL;
   struct message* kmsg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   config->connections[slot].security_lengths[0] = msg->length;
   memcpy(&config->connections[slot].security_messages[0], msg->data, msg->length);

   if (auth_type == SECURITY_TRUST)
   {
      ret = server_trust(slot, server_ssl);
   }
   else if (auth_type == SECURITY_PASSWORD)
   {
      ret = server_password(username, password, slot, server_ssl);
   }
   else if (auth_type == SECURITY_MD5)
   {
      ret = server_md5(username, password, slot, server_ssl);
   }
   else if (auth_type == SECURITY_SCRAM256)
   {
      ret = server_scram256(username, password, slot, server_ssl);
   }

   if (config->connections[slot].has_security == SECURITY_TRUST)
   {
      pgagroal_create_message(&config->connections[slot].security_messages[0],
                              config->connections[slot].security_lengths[0],
                              &smsg);
   }
   else if (config->connections[slot].has_security == SECURITY_PASSWORD || config->connections[slot].has_security == SECURITY_MD5)
   {
      pgagroal_create_message(&config->connections[slot].security_messages[2],
                              config->connections[slot].security_lengths[2],
                              &smsg);
   }
   else if (config->connections[slot].has_security == SECURITY_SCRAM256)
   {
      pgagroal_create_message(&config->connections[slot].security_messages[4],
                              config->connections[slot].security_lengths[4],
                              &smsg);
   }

   if (smsg != NULL)
   {
      pgagroal_extract_message('K', smsg, &kmsg);

      if (kmsg != NULL)
      {
         config->connections[slot].backend_pid = pgagroal_read_int32(kmsg->data + 5);
         config->connections[slot].backend_secret = pgagroal_read_int32(kmsg->data + 9);
      }
   }

   pgagroal_free_copy_message(smsg);
   pgagroal_free_copy_message(kmsg);

   return ret;

error:

   pgagroal_log_error("server_authenticate: %d", auth_type);

   pgagroal_free_copy_message(smsg);
   pgagroal_free_copy_message(kmsg);

   return AUTH_ERROR;
}

static int
server_trust(int slot, SSL* server_ssl)
{
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   pgagroal_log_trace("server_trust");

   config->connections[slot].has_security = SECURITY_TRUST;

   return AUTH_SUCCESS;
}

static int
server_password(char* username, char* password, int slot, SSL* server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int auth_response = -1;
   int server_fd;
   struct message* auth_msg = NULL;
   struct message* password_msg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   pgagroal_log_trace("server_password");

   status = pgagroal_create_auth_password_response(password, &password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(server_ssl, server_fd, password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = password_msg->length;
   memcpy(&config->connections[slot].security_messages[auth_index], password_msg->data, password_msg->length);
   auth_index++;

   status = pgagroal_read_block_message(server_ssl, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgagroal_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgagroal_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }

      config->connections[slot].security_lengths[auth_index] = auth_msg->length;
      memcpy(&config->connections[slot].security_messages[auth_index], auth_msg->data, auth_msg->length);

      config->connections[slot].has_security = SECURITY_PASSWORD;
   }
   else
   {
      goto bad_password;
   }

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_warn("Wrong password for user: %s", username);

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_ERROR;
}

static int
server_md5(char* username, char* password, int slot, SSL* server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int auth_response = -1;
   int server_fd;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   char md5str[36];
   char* salt = NULL;
   struct message* auth_msg = NULL;
   struct message* md5_msg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   pgagroal_log_trace("server_md5");

   if (get_salt(config->connections[slot].security_messages[0], &salt))
   {
      goto error;
   }

   size = strlen(username) + strlen(password) + 1;
   pwdusr = malloc(size);
   memset(pwdusr, 0, size);

   snprintf(pwdusr, size, "%s%s", password, username);

   if (pgagroal_md5(pwdusr, strlen(pwdusr), &shadow))
   {
      goto error;
   }

   md5_req = malloc(36);
   memset(md5_req, 0, 36);
   memcpy(md5_req, shadow, 32);
   memcpy(md5_req + 32, salt, 4);

   if (pgagroal_md5(md5_req, 36, &md5))
   {
      goto error;
   }

   memset(&md5str, 0, sizeof(md5str));
   snprintf(&md5str[0], 36, "md5%s", md5);

   status = pgagroal_create_auth_md5_response(md5str, &md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(server_ssl, server_fd, md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = md5_msg->length;
   memcpy(&config->connections[slot].security_messages[auth_index], md5_msg->data, md5_msg->length);
   auth_index++;

   status = pgagroal_read_block_message(server_ssl, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgagroal_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgagroal_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }

      config->connections[slot].security_lengths[auth_index] = auth_msg->length;
      memcpy(&config->connections[slot].security_messages[auth_index], auth_msg->data, auth_msg->length);

      config->connections[slot].has_security = SECURITY_MD5;
   }
   else
   {
      goto bad_password;
   }

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_warn("Wrong password for user: %s", username);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_BAD_PASSWORD;

error:

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_ERROR;
}

static int
server_scram256(char* username, char* password, int slot, SSL* server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int server_fd;
   char* salt = NULL;
   int salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   int proof_length;
   char* proof_base = NULL;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   int server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   pgagroal_log_trace("server_scram256");

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgagroal_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = sasl_response->length;
   memcpy(&config->connections[slot].security_messages[auth_index], sasl_response->data, sasl_response->length);
   auth_index++;

   status = pgagroal_write_message(server_ssl, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(server_ssl, server_fd, &msg);
   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   sasl_continue = pgagroal_copy_message(msg);

   config->connections[slot].security_lengths[auth_index] = sasl_continue->length;
   memcpy(&config->connections[slot].security_messages[auth_index], sasl_continue->data, sasl_continue->length);
   auth_index++;

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      pgagroal_log_error("SCRAM-SHA-256: %s", err);
      goto error;
   }

   pgagroal_base64_decode(base64_salt, strlen(base64_salt), &salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = config->connections[slot].security_messages[1] + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = config->connections[slot].security_messages[2] + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, config->connections[slot].security_lengths[1] - 26,
                    server_first_message, config->connections[slot].security_lengths[2] - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgagroal_base64_encode((char*)proof, proof_length, &proof_base);

   status = pgagroal_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = sasl_continue_response->length;
   memcpy(&config->connections[slot].security_messages[auth_index], sasl_continue_response->data, sasl_continue_response->length);
   auth_index++;

   status = pgagroal_write_message(server_ssl, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(server_ssl, server_fd, &msg);
   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = msg->length;
   memcpy(&config->connections[slot].security_messages[auth_index], msg->data, msg->length);
   auth_index++;

   if (pgagroal_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgagroal_base64_decode(base64_server_signature, sasl_final->length - 11,
                          &server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, config->connections[slot].security_lengths[1] - 26,
                        server_first_message, config->connections[slot].security_lengths[2] - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   config->connections[slot].has_security = SECURITY_SCRAM256;

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_warn("Wrong password for user: %s", username);

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_ERROR;
}

static bool
is_allowed(char* username, char* database, char* address, int* hba_method)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_hbas; i++)
   {
      if (is_allowed_address(address, config->hbas[i].address) &&
          is_allowed_database(database, config->hbas[i].database) &&
          is_allowed_username(username, config->hbas[i].username))
      {
         *hba_method = get_hba_method(i);

         return true;
      }
   }

   return false;
}

static bool
is_allowed_username(char* username, char* entry)
{
   if (!strcasecmp(entry, "all") || !strcmp(username, entry))
   {
      return true;
   }

   return false;
}

static bool
is_allowed_database(char* database, char* entry)
{
   if (!strcasecmp(entry, "all") || !strcmp(database, entry))
   {
      return true;
   }

   return false;
}

static bool
is_allowed_address(char* address, char* entry)
{
   struct sockaddr_in address_sa4;
   struct sockaddr_in6 address_sa6;
   struct sockaddr_in entry_sa4;
   struct sockaddr_in6 entry_sa6;
   char addr[INET6_ADDRSTRLEN];
   char s_mask[4];
   int mask;
   char* marker;
   bool ipv4 = true;

   memset(&addr, 0, sizeof(addr));
   memset(&s_mask, 0, sizeof(s_mask));

   if (!strcasecmp(entry, "all"))
   {
      return true;
   }

   marker = strchr(entry, '/');
   if (!marker)
   {
      pgagroal_log_warn("Invalid HBA entry: %s", entry);
      return false;
   }

   memcpy(&addr, entry, marker - entry);
   marker += sizeof(char);
   memcpy(&s_mask, marker, strlen(marker));
   mask = atoi(s_mask);

   if (strchr(addr, ':') == NULL)
   {
      inet_pton(AF_INET, addr, &(entry_sa4.sin_addr));

      if (strchr(address, ':') == NULL)
      {
         inet_pton(AF_INET, address, &(address_sa4.sin_addr));
      }
      else
      {
         return false;
      }
   }
   else
   {
      ipv4 = false;

      inet_pton(AF_INET6, addr, &(entry_sa6.sin6_addr));

      if (strchr(address, ':') != NULL)
      {
         inet_pton(AF_INET6, address, &(address_sa6.sin6_addr));
      }
      else
      {
         return false;
      }
   }

   if (ipv4)
   {
      if (!strcmp(entry, "0.0.0.0/0"))
      {
         return true;
      }

      if (mask < 0 || mask > 32)
      {
         pgagroal_log_warn("Invalid HBA entry: %s", entry);
         return false;
      }

      unsigned char a1 = (ntohl(address_sa4.sin_addr.s_addr) >> 24) & 0xff;
      unsigned char a2 = (ntohl(address_sa4.sin_addr.s_addr) >> 16) & 0xff;
      unsigned char a3 = (ntohl(address_sa4.sin_addr.s_addr) >> 8) & 0xff;
      unsigned char a4 = (ntohl(address_sa4.sin_addr.s_addr)) & 0xff;

      unsigned char e1 = (ntohl(entry_sa4.sin_addr.s_addr) >> 24) & 0xff;
      unsigned char e2 = (ntohl(entry_sa4.sin_addr.s_addr) >> 16) & 0xff;
      unsigned char e3 = (ntohl(entry_sa4.sin_addr.s_addr) >> 8) & 0xff;
      unsigned char e4 = (ntohl(entry_sa4.sin_addr.s_addr)) & 0xff;

      if (mask <= 8)
      {
         return a1 == e1;
      }
      else if (mask <= 16)
      {
         return a1 == e1 && a2 == e2;
      }
      else if (mask <= 24)
      {
         return a1 == e1 && a2 == e2 && a3 == e3;
      }
      else
      {
         return a1 == e1 && a2 == e2 && a3 == e3 && a4 == e4;
      }
   }
   else
   {
      if (!strcmp(entry, "::0/0"))
      {
         return true;
      }

      if (mask < 0 || mask > 128)
      {
         pgagroal_log_warn("Invalid HBA entry: %s", entry);
         return false;
      }

      struct sockaddr_in6 netmask;
      bool result = false;

      memset(&netmask, 0, sizeof(struct sockaddr_in6));

      for (long i = mask, j = 0; i > 0; i -= 8, ++j)
      {
         netmask.sin6_addr.s6_addr[j] = i >= 8 ? 0xff : (unsigned long int)((0xffU << (8 - i)) & 0xffU);
      }

      for (unsigned i = 0; i < 16; i++)
      {
         result |= (0 != (address_sa6.sin6_addr.s6_addr[i] & !netmask.sin6_addr.s6_addr[i]));
      }

      return result;
   }

   return false;
}

static bool
is_disabled(char* database)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_DISABLED; i++)
   {
      if (!strcmp(config->disabled[i], "*") ||
          !strcmp(config->disabled[i], database))
      {
         return true;
      }
   }

   return false;
}

static int
get_hba_method(int index)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!strcasecmp(config->hbas[index].method, "reject"))
   {
      return SECURITY_REJECT;
   }

   if (!strcasecmp(config->hbas[index].method, "trust"))
   {
      return SECURITY_TRUST;
   }

   if (!strcasecmp(config->hbas[index].method, "password"))
   {
      return SECURITY_PASSWORD;
   }

   if (!strcasecmp(config->hbas[index].method, "md5"))
   {
      return SECURITY_MD5;
   }

   if (!strcasecmp(config->hbas[index].method, "scram-sha-256"))
   {
      return SECURITY_SCRAM256;
   }

   if (!strcasecmp(config->hbas[index].method, "all"))
   {
      return SECURITY_ALL;
   }

   return SECURITY_REJECT;
}

static char*
get_password(char* username)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_users; i++)
   {
      if (!strcmp(&config->users[i].username[0], username))
      {
         return &config->users[i].password[0];
      }
   }

   return NULL;
}

static char*
get_frontend_password(char* username)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_frontend_users; i++)
   {
      if (!strcmp(&config->frontend_users[i].username[0], username))
      {
         return &config->frontend_users[i].password[0];
      }
   }

   return NULL;
}

static char*
get_admin_password(char* username)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_admins; i++)
   {
      if (!strcmp(&config->admins[i].username[0], username))
      {
         return &config->admins[i].password[0];
      }
   }

   return NULL;
}

static int
get_salt(void* data, char** salt)
{
   char* result;

   result = malloc(4);
   memset(result, 0, 4);

   memcpy(result, data + 9, 4);

   *salt = result;

   return 0;
}

int
pgagroal_get_master_key(char** masterkey)
{
   FILE* master_key_file = NULL;
   char buf[MISC_LENGTH];
   char line[MISC_LENGTH];
   char* mk = NULL;
   int mk_length = 0;
   struct stat st = {0};

   if (pgagroal_get_home_directory() == NULL)
   {
      goto error;
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgagroal", pgagroal_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      goto error;
   }
   else
   {
      if (S_ISDIR(st.st_mode) && st.st_mode & S_IRWXU && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         goto error;
      }
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgagroal/master.key", pgagroal_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      goto error;
   }
   else
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         goto error;
      }
   }

   master_key_file = fopen(&buf[0], "r");
   if (master_key_file == NULL)
   {
      goto error;
   }

   memset(&line, 0, sizeof(line));
   if (fgets(line, sizeof(line), master_key_file) == NULL)
   {
      goto error;
   }

   pgagroal_base64_decode(&line[0], strlen(&line[0]), &mk, &mk_length);

   *masterkey = mk;

   fclose(master_key_file);

   return 0;

error:

   free(mk);

   if (master_key_file)
   {
      fclose(master_key_file);
   }

   return 1;
}

int
pgagroal_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv) != 0)
   {
      return 1;
   }

   return aes_encrypt(plaintext, key, iv, ciphertext, ciphertext_length);
}

int
pgagroal_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv) != 0)
   {
      return 1;
   }

   return aes_decrypt(ciphertext, ciphertext_length, key, iv, plaintext);
}

int
pgagroal_md5(char* str, int length, char** md5)
{
   int n;
   MD5_CTX c;
   unsigned char digest[16];
   char* out;

   out = malloc(33);

   memset(out, 0, 33);

   MD5_Init(&c);
   MD5_Update(&c, str, length);
   MD5_Final(digest, &c);

   for (n = 0; n < 16; ++n)
   {
      snprintf(&(out[n * 2]), 32, "%02x", (unsigned int)digest[n]);
   }

   *md5 = out;

   return 0;
}

bool
pgagroal_user_known(char* user)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_users; i++)
   {
      if (!strcmp(user, config->users[i].username))
      {
         return true;
      }
   }

   return false;
}

int
pgagroal_tls_valid(void)
{
   struct configuration* config;
   struct stat st = {0};

   config = (struct configuration*)shmem;

   if (config->tls)
   {
      if (strlen(config->tls_cert_file) == 0)
      {
         pgagroal_log_error("No TLS certificate defined");
         goto error;
      }

      if (strlen(config->tls_key_file) == 0)
      {
         pgagroal_log_error("No TLS private key defined");
         goto error;
      }

      if (stat(config->tls_cert_file, &st) == -1)
      {
         pgagroal_log_error("Can't locate TLS certificate file: %s", config->tls_cert_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgagroal_log_error("TLS certificate file is not a regular file: %s", config->tls_cert_file);
         goto error;
      }

      if (st.st_uid && st.st_uid != geteuid())
      {
         pgagroal_log_error("TLS certificate file not owned by user or root: %s", config->tls_cert_file);
         goto error;
      }

      memset(&st, 0, sizeof(struct stat));

      if (stat(config->tls_key_file, &st) == -1)
      {
         pgagroal_log_error("Can't locate TLS private key file: %s", config->tls_key_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgagroal_log_error("TLS private key file is not a regular file: %s", config->tls_key_file);
         goto error;
      }

      if (st.st_uid == geteuid())
      {
         if (st.st_mode & (S_IRWXG | S_IRWXO))
         {
            pgagroal_log_error("TLS private key file must have 0600 permissions when owned by a non-root user: %s", config->tls_key_file);
            goto error;
         }
      }
      else if (st.st_uid == 0)
      {
         if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO))
         {
            pgagroal_log_error("TLS private key file must have at least 0640 permissions when owned by root: %s", config->tls_key_file);
            goto error;
         }

      }
      else
      {
         pgagroal_log_error("TLS private key file not owned by user or root: %s", config->tls_key_file);
         goto error;
      }

      if (strlen(config->tls_ca_file) > 0)
      {
         memset(&st, 0, sizeof(struct stat));

         if (stat(config->tls_ca_file, &st) == -1)
         {
            pgagroal_log_error("Can't locate TLS CA file: %s", config->tls_ca_file);
            goto error;
         }

         if (!S_ISREG(st.st_mode))
         {
            pgagroal_log_error("TLS CA file is not a regular file: %s", config->tls_ca_file);
            goto error;
         }

         if (st.st_uid && st.st_uid != geteuid())
         {
            pgagroal_log_error("TLS CA file not owned by user or root: %s", config->tls_ca_file);
            goto error;
         }
      }
      else
      {
         pgagroal_log_debug("No TLS CA file");
      }
   }

   return 0;

error:

   return 1;
}

static int
derive_key_iv(char* password, unsigned char* key, unsigned char* iv)
{

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   OpenSSL_add_all_algorithms();
#endif

   if (!EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha1(), NULL,
                       (unsigned char*) password, strlen(password), 1,
                       key, iv))
   {
      return 1;
   }

   return 0;
}

static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int length;
   size_t size;
   unsigned char* ct = NULL;
   int ct_length;

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = strlen(plaintext) + EVP_CIPHER_block_size(EVP_aes_256_cbc());
   ct = malloc(size);
   memset(ct, 0, size);

   if (EVP_EncryptUpdate(ctx,
                         ct, &length,
                         (unsigned char*)plaintext, strlen((char*)plaintext)) != 1)
   {
      goto error;
   }

   ct_length = length;

   if (EVP_EncryptFinal_ex(ctx, ct + length, &length) != 1)
   {
      goto error;
   }

   ct_length += length;

   EVP_CIPHER_CTX_free(ctx);

   *ciphertext = (char*)ct;
   *ciphertext_length = ct_length;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   free(ct);

   return 1;
}

static int
aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int plaintext_length;
   int length;
   size_t size;
   char* pt = NULL;

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = ciphertext_length + EVP_CIPHER_block_size(EVP_aes_256_cbc());
   pt = malloc(size);
   memset(pt, 0, size);

   if (EVP_DecryptUpdate(ctx,
                         (unsigned char*)pt, &length,
                         (unsigned char*)ciphertext, ciphertext_length) != 1)
   {
      goto error;
   }

   plaintext_length = length;

   if (EVP_DecryptFinal_ex(ctx, (unsigned char*)pt + length, &length) != 1)
   {
      goto error;
   }

   plaintext_length += length;

   EVP_CIPHER_CTX_free(ctx);

   pt[plaintext_length] = 0;
   *plaintext = pt;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   free(pt);

   return 1;
}

static int
sasl_prep(char* password, char** password_prep)
{
   char* p = NULL;

   /* Only support ASCII for now */
   for (int i = 0; i < strlen(password); i++)
   {
      if ((unsigned char)(*(password + i)) & 0x80)
      {
         goto error;
      }
   }

   p = strdup(password);

   *password_prep = p;

   return 0;

error:

   *password_prep = NULL;

   return 1;
}

static int
generate_nounce(char** nounce)
{
   size_t s = 18;
   unsigned char r[s + 1];
   char* base = NULL;
   int result;

   memset(&r[0], 0, sizeof(r));

   result = RAND_bytes(r, sizeof(r));
   if (result != 1)
   {
      goto error;
   }

   r[s] = '\0';

   pgagroal_base64_encode((char*)&r[0], s, &base);

   *nounce = base;

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 0;

error:

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 1;
}

static int
get_scram_attribute(char attribute, char* input, size_t size, char** value)
{
   char* dup = NULL;
   char* result = NULL;
   char* ptr = NULL;
   size_t token_size;
   char match[2];

   match[0] = attribute;
   match[1] = '=';

   dup = (char*)malloc(size + 1);
   memset(dup, 0, size + 1);
   memcpy(dup, input, size);

   ptr = strtok(dup, ",");
   while (ptr != NULL)
   {
      if (!strncmp(ptr, &match[0], 2))
      {
         token_size = strlen(ptr) - 1;
         result = malloc(token_size);
         memset(result, 0, token_size);
         memcpy(result, ptr + 2, token_size);
         goto done;
      }

      ptr = strtok(NULL, ",");
   }

   if (result == NULL)
   {
      goto error;
   }

done:

   *value = result;

   free(dup);

   return 0;

error:

   *value = NULL;

   free(dup);

   return 1;
}

static int
client_proof(char* password, char* salt, int salt_length, int iterations,
             char* client_first_message_bare, size_t client_first_message_bare_length,
             char* server_first_message, size_t server_first_message_length,
             char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
             unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* c_k = NULL;
   int c_k_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned char* c_s = NULL;
   unsigned int length;
   unsigned char* r = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
   {
      goto error;
   }

   if (salted_password_key(s_p, s_p_length, "Client Key", &c_k, &c_k_length))
   {
      goto error;
   }

   if (stored_key(c_k, c_k_length, &s_k, &s_k_length))
   {
      goto error;
   }

   c_s = malloc(size);
   memset(c_s, 0, size);

   r = malloc(size);
   memset(r, 0, size);

   /* Client signature: HMAC(StoredKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, c_s, &length) != 1)
   {
      goto error;
   }

   /* ClientProof: ClientKey XOR ClientSignature */
   for (int i = 0; i < size; i++)
   {
      *(r + i) = *(c_k + i) ^ *(c_s + i);
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   free(s_p);
   free(c_k);
   free(s_k);
   free(c_s);

   return 0;

error:

   *result = NULL;
   *result_length = 0;

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   free(s_p);
   free(c_k);
   free(s_k);
   free(c_s);

   return 1;
}

static int
verify_client_proof(char* s_key, int s_key_length,
                    char* client_proof, int client_proof_length,
                    char* salt, int salt_length, int iterations,
                    char* client_first_message_bare, size_t client_first_message_bare_length,
                    char* server_first_message, size_t server_first_message_length,
                    char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length)
{
   size_t size = 32;
   unsigned char* c_k = NULL;
   int c_k_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned char* c_s = NULL;
   unsigned int length;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   /* if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length)) */
   /* { */
   /*    goto error; */
   /* } */

   /* if (salted_password_key(s_p, s_p_length, "Client Key", &c_k, &c_k_length)) */
   /* { */
   /*    goto error; */
   /* } */

   c_k = malloc(size);
   memset(c_k, 0, size);
   c_k_length = size;

   c_s = malloc(size);
   memset(c_s, 0, size);

   /* Client signature: HMAC(StoredKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_key, s_key_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, c_s, &length) != 1)
   {
      goto error;
   }

   /* ClientKey: ClientProof XOR ClientSignature */
   for (int i = 0; i < size; i++)
   {
      *(c_k + i) = *(client_proof + i) ^ *(c_s + i);
   }

   if (stored_key(c_k, c_k_length, &s_k, &s_k_length))
   {
      goto error;
   }

   if (memcmp(s_key, s_k, size) != 0)
   {
      goto error;
   }

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   free(c_k);
   free(s_k);
   free(c_s);

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   free(c_k);
   free(s_k);
   free(c_s);

   return 1;
}

static int
salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length)
{
   size_t size = 32;
   int password_length;
   unsigned int one;
   unsigned char Ui[size];
   unsigned char Ui_prev[size];
   unsigned int Ui_length;
   unsigned char* r = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   password_length = strlen(password);

   if (!pgagroal_bigendian())
   {
      one = pgagroal_swap(1);
   }
   else
   {
      one = 1;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* SaltedPassword: Hi(Normalize(password), salt, iterations) */
   if (HMAC_Init_ex(ctx, password, password_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)salt, salt_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)&one, sizeof(one)) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, &Ui_prev[0], &Ui_length) != 1)
   {
      goto error;
   }
   memcpy(r, &Ui_prev[0], size);

   for (int i = 2; i <= iterations; i++)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      if (HMAC_CTX_reset(ctx) != 1)
      {
         goto error;
      }
#endif

      if (HMAC_Init_ex(ctx, password, password_length, EVP_sha256(), NULL) != 1)
      {
         goto error;
      }

      if (HMAC_Update(ctx, &Ui_prev[0], size) != 1)
      {
         goto error;
      }

      if (HMAC_Final(ctx, &Ui[0], &Ui_length) != 1)
      {
         goto error;
      }

      for (int j = 0; j < size; j++)
      {
         *(r + j) ^= *(Ui + j);
      }
      memcpy(&Ui_prev[0], &Ui[0], size);
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
salted_password_key(unsigned char* salted_password, int salted_password_length, char* key, unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned int length;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* HMAC(SaltedPassword, Key) */
   if (HMAC_Init_ex(ctx, salted_password, salted_password_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)key, strlen(key)) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned int length;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   EVP_MD_CTX* ctx = EVP_MD_CTX_new();
#else
   EVP_MD_CTX* ctx = EVP_MD_CTX_create();

   EVP_MD_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* StoredKey: H(ClientKey) */
   if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (EVP_DigestUpdate(ctx, client_key, client_key_length) != 1)
   {
      goto error;
   }

   if (EVP_DigestFinal_ex(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   EVP_MD_CTX_free(ctx);
#else
   EVP_MD_CTX_destroy(ctx);
#endif

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      EVP_MD_CTX_free(ctx);
#else
      EVP_MD_CTX_destroy(ctx);
#endif
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
generate_salt(char** salt, int* size)
{
   size_t s = 16;
   unsigned char* r = NULL;
   int result;

   r = malloc(s);
   memset(r, 0, s);

   result = RAND_bytes(r, s);
   if (result != 1)
   {
      goto error;
   }

   *salt = (char*)r;
   *size = s;

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 0;

error:

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   free(r);

   *salt = NULL;
   *size = 0;

   return 1;
}

static int
server_signature(char* password, char* salt, int salt_length, int iterations,
                 char* s_key, int s_key_length,
                 char* client_first_message_bare, size_t client_first_message_bare_length,
                 char* server_first_message, size_t server_first_message_length,
                 char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                 unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned int length;
   bool do_free = true;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   if (password != NULL)
   {
      if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
      {
         goto error;
      }

      if (salted_password_key(s_p, s_p_length, "Server Key", &s_k, &s_k_length))
      {
         goto error;
      }
   }
   else
   {
      do_free = false;
      s_k = (unsigned char*)s_key;
      s_k_length = s_key_length;
   }

   /* Server signature: HMAC(ServerKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = length;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   free(s_p);
   if (do_free)
   {
      free(s_k);
   }

   return 0;

error:

   *result = NULL;
   *result_length = 0;

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   free(s_p);
   if (do_free)
   {
      free(s_k);
   }

   return 1;
}

static bool
is_tls_user(char* username, char* database)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_hbas; i++)
   {
      if ((!strcmp(database, config->hbas[i].database) || !strcmp("all", config->hbas[i].database)) &&
          (!strcmp(username, config->hbas[i].username) || !strcmp("all", config->hbas[i].username)))
      {
         if (!strcmp("hostssl", config->hbas[i].type))
         {
            return true;
         }
      }
   }

   return false;
}

static int
create_ssl_ctx(bool client, SSL_CTX** ctx)
{
   SSL_CTX* c = NULL;

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   OpenSSL_add_all_algorithms();
#endif

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   if (client)
   {
      c = SSL_CTX_new(TLSv1_2_client_method());
   }
   else
   {
      c = SSL_CTX_new(TLSv1_2_server_method());
   }
#else
   if (client)
   {
      c = SSL_CTX_new(TLS_client_method());
   }
   else
   {
      c = SSL_CTX_new(TLS_server_method());
   }
#endif

   if (c == NULL)
   {
      goto error;
   }

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   SSL_CTX_set_options(c, SSL_OP_NO_SSLv3);
   SSL_CTX_set_options(c, SSL_OP_NO_TLSv1);
   SSL_CTX_set_options(c, SSL_OP_NO_TLSv1_1);
#else
   if (SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION) == 0)
   {
      goto error;
   }
#endif

   SSL_CTX_set_mode(c, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
   SSL_CTX_set_options(c, SSL_OP_NO_TICKET);
   SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);

   *ctx = c;

   return 0;

error:

   if (c != NULL)
   {
      SSL_CTX_free(c);
   }

   return 1;
}

static int
create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl)
{
   SSL* s = NULL;
   bool have_cert = false;
   bool have_rootcert = false;

   if (root != NULL && strlen(root) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, root, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", root);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_rootcert = true;
   }

   if (cert != NULL && strlen(cert) > 0)
   {
      if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS certificate: %s", cert);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_cert = true;
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   if (have_cert && strlen(key) > 0)
   {
      if (SSL_use_PrivateKey_file(s, key, SSL_FILETYPE_PEM) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS private key: %s", key);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      if (SSL_check_private_key(s) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("TLS private key check failed: %s", key);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }
   }

   if (have_rootcert)
   {
      SSL_set_verify(s, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
   }

   *ssl = s;

   return 0;

error:

   if (s != NULL)
   {
      SSL_shutdown(s);
      SSL_free(s);
   }
   SSL_CTX_free(ctx);

   return 1;
}

static int
create_ssl_server(SSL_CTX* ctx, int socket, SSL** ssl)
{
   SSL* s = NULL;
   STACK_OF(X509_NAME) * root_cert_list = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->tls_cert_file) == 0)
   {
      pgagroal_log_error("No TLS certificate defined");
      goto error;
   }

   if (strlen(config->tls_key_file) == 0)
   {
      pgagroal_log_error("No TLS private key defined");
      goto error;
   }

   if (SSL_CTX_use_certificate_chain_file(ctx, config->tls_cert_file) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("Couldn't load TLS certificate: %s", config->tls_cert_file);
      pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_use_PrivateKey_file(ctx, config->tls_key_file, SSL_FILETYPE_PEM) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("Couldn't load TLS private key: %s", config->tls_key_file);
      pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_check_private_key(ctx) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("TLS private key check failed: %s", config->tls_key_file);
      pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (strlen(config->tls_ca_file) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, config->tls_ca_file, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", config->tls_ca_file);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      root_cert_list = SSL_load_client_CA_file(config->tls_ca_file);
      if (root_cert_list == NULL)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", config->tls_ca_file);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      SSL_CTX_set_verify(ctx, (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE), NULL);
      SSL_CTX_set_client_CA_list(ctx, root_cert_list);
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   *ssl = s;

   return 0;

error:

   if (s != NULL)
   {
      SSL_shutdown(s);
      SSL_free(s);
   }
   SSL_CTX_free(ctx);

   return 1;
}

static int
auth_query(SSL* c_ssl, int client_fd, int slot, char* username, char* database, int hba_method)
{
   int su_socket;
   SSL* su_ssl = NULL;
   char* shadow = NULL;
   int ret;
   struct configuration* config;

   config = (struct configuration*)shmem;

   /* Get connection to server using the superuser */
   ret = auth_query_get_connection(config->superuser.username, config->superuser.password, database, &su_socket, &su_ssl);
   if (ret == AUTH_BAD_PASSWORD)
   {
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      atomic_store(&config->su_connection, STATE_FREE);
      goto bad_password;
   }
   else if (ret == AUTH_ERROR)
   {
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      atomic_store(&config->su_connection, STATE_FREE);
      goto error;
   }
   else if (ret == AUTH_TIMEOUT)
   {
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      atomic_store(&config->su_connection, STATE_FREE);
      goto error;
   }

   /* Call pgagroal_get_password */
   if (auth_query_get_password(su_socket, su_ssl, username, database, &shadow))
   {
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      atomic_store(&config->su_connection, STATE_FREE);
      goto error;
   }

   /* Close connection */
   pgagroal_disconnect(su_socket);
   atomic_store(&config->su_connection, STATE_FREE);

   /* Client security */
   if (config->connections[slot].has_security == SECURITY_MD5)
   {
      ret = auth_query_client_md5(c_ssl, client_fd, username, shadow, slot);
      if (ret == AUTH_BAD_PASSWORD)
      {
         pgagroal_write_bad_password(c_ssl, client_fd, username);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }
      else if (ret == AUTH_ERROR)
      {
         goto error;
      }
   }
   else if (config->connections[slot].has_security == SECURITY_SCRAM256)
   {
      ret = auth_query_client_scram256(c_ssl, client_fd, username, shadow, slot);
      if (ret == AUTH_BAD_PASSWORD)
      {
         pgagroal_write_bad_password(c_ssl, client_fd, username);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }
      else if (ret == AUTH_ERROR)
      {
         goto error;
      }
   }
   else
   {
      pgagroal_log_error("Authentication query not supported: %d", config->connections[slot].has_security);
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }

   /* Client ok */
   if (client_ok(c_ssl, client_fd, slot))
   {
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }

   free(shadow);

   return AUTH_SUCCESS;

bad_password:

   free(shadow);

   return AUTH_BAD_PASSWORD;

error:

   free(shadow);

   return AUTH_ERROR;
}

static int
auth_query_get_connection(char* username, char* password, char* database, int* server_fd, SSL** server_ssl)
{
   int auth_type = -1;
   int server;
   signed char isfree;
   time_t start_time;
   char* error = NULL;
   struct configuration* config = NULL;
   struct message* startup_msg = NULL;
   struct message* startup_response_msg = NULL;
   struct message* msg = NULL;
   int ret = -1;
   int status = -1;

   config = (struct configuration*)shmem;

   *server_fd = -1;

   /* We need to find the server for the connection */
   if (pgagroal_get_primary(&server))
   {
      pgagroal_log_error("pgagroal: No valid server available");
      goto error;
   }
   pgagroal_log_debug("connect: server %d", server);

   start_time = time(NULL);

retry:

   isfree = STATE_FREE;

   if (atomic_compare_exchange_strong(&config->su_connection, &isfree, STATE_IN_USE))
   {
      if (config->servers[server].host[0] == '/')
      {
         char pgsql[MISC_LENGTH];

         memset(&pgsql, 0, sizeof(pgsql));
         snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
         ret = pgagroal_connect_unix_socket(config->servers[server].host, &pgsql[0], server_fd);
      }
      else
      {
         ret = pgagroal_connect(config->servers[server].host, config->servers[server].port, server_fd);
      }

      if (ret)
      {
         pgagroal_log_error("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
         atomic_store(&config->su_connection, STATE_FREE);
         goto error;
      }
   }
   else
   {
      if (config->blocking_timeout > 0)
      {
         /* Sleep for 100ms */
         struct timespec ts;
         ts.tv_sec = 0;
         ts.tv_nsec = 100000000L;
         nanosleep(&ts, NULL);

         double diff = difftime(time(NULL), start_time);
         if (diff >= (double)config->blocking_timeout)
         {
            goto timeout;
         }

         goto retry;
      }
      else
      {
         goto timeout;
      }
   }

   pgagroal_log_debug("connect: %s:%d using fd %d", config->servers[server].host, config->servers[server].port, *server_fd);

   /* TLS support */
   establish_client_tls_connection(server, *server_fd, server_ssl);

   /* Startup message */
   status = pgagroal_create_startup_message(username, database, &startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(*server_ssl, *server_fd, startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(*server_ssl, *server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   startup_response_msg = pgagroal_copy_message(msg);

   get_auth_type(msg, &auth_type);
   pgagroal_log_trace("auth_query_get_connection: auth type %d", auth_type);

   /* Supported security models: */
   /*   md5 (5) */
   /*   scram256 (10) */
   if (auth_type == SECURITY_MD5)
   {
      ret = auth_query_server_md5(startup_response_msg, username, password, *server_fd, *server_ssl);
      if (ret == AUTH_BAD_PASSWORD)
      {
         goto bad_password;
      }
      else if (ret == AUTH_ERROR)
      {
         goto error;
      }
   }
   else if (auth_type == SECURITY_SCRAM256)
   {
      ret = auth_query_server_scram256(username, password, *server_fd, *server_ssl);
      if (ret == AUTH_BAD_PASSWORD)
      {
         goto bad_password;
      }
      else if (ret == AUTH_ERROR)
      {
         goto error;
      }
   }
   else
   {
      if (msg->kind == 'E')
      {
         if (pgagroal_extract_error_message(msg, &error))
         {
            goto error;
         }
         pgagroal_log_error("%s", error);
      }

      goto error;
   }

   free(error);

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(startup_response_msg);
   pgagroal_free_message(msg);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_debug("auth_query_get_connection: BAD_PASSWORD");

   if (*server_fd != -1)
   {
      pgagroal_disconnect(*server_fd);
   }

   *server_fd = -1;

   free(error);

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(startup_response_msg);
   pgagroal_free_message(msg);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_log_debug("auth_query_get_connection: ERROR (%d)", auth_type);

   if (*server_fd != -1)
   {
      pgagroal_disconnect(*server_fd);
   }

   *server_fd = -1;

   free(error);

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(startup_response_msg);
   pgagroal_free_message(msg);

   return AUTH_ERROR;

timeout:

   pgagroal_log_debug("auth_query_get_connection: TIMEOUT");

   *server_fd = -1;

   free(error);

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_copy_message(startup_response_msg);
   pgagroal_free_message(msg);

   return AUTH_TIMEOUT;
}

static int
auth_query_server_md5(struct message* startup_response_msg, char* username, char* password, int socket, SSL* server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_response = -1;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   char md5str[36];
   char* salt = NULL;
   struct message* auth_msg = NULL;
   struct message* md5_msg = NULL;

   pgagroal_log_trace("auth_query_server_md5");

   if (get_salt(startup_response_msg->data, &salt))
   {
      goto error;
   }

   size = strlen(username) + strlen(password) + 1;
   pwdusr = malloc(size);
   memset(pwdusr, 0, size);

   snprintf(pwdusr, size, "%s%s", password, username);

   if (pgagroal_md5(pwdusr, strlen(pwdusr), &shadow))
   {
      goto error;
   }

   md5_req = malloc(36);
   memset(md5_req, 0, 36);
   memcpy(md5_req, shadow, 32);
   memcpy(md5_req + 32, salt, 4);

   if (pgagroal_md5(md5_req, 36, &md5))
   {
      goto error;
   }

   memset(&md5str, 0, sizeof(md5str));
   snprintf(&md5str[0], 36, "md5%s", md5);

   status = pgagroal_create_auth_md5_response(md5str, &md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(server_ssl, socket, md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(server_ssl, socket, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgagroal_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgagroal_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgagroal_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }
   }
   else
   {
      goto bad_password;
   }

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_warn("Wrong password for user: %s", username);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_BAD_PASSWORD;

error:

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_ERROR;
}

static int
auth_query_server_scram256(char* username, char* password, int socket, SSL* server_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   char* salt = NULL;
   int salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   int proof_length;
   char* proof_base = NULL;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   int server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length;
   char* error = NULL;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;

   pgagroal_log_trace("auth_query_server_scram256");

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgagroal_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(server_ssl, socket, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(server_ssl, socket, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgagroal_copy_message(msg);

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      pgagroal_log_error("SCRAM-SHA-256: %s", err);
      goto error;
   }

   pgagroal_base64_decode(base64_salt, strlen(base64_salt), &salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = sasl_response->data + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = sasl_continue->data + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, sasl_response->length - 26,
                    server_first_message, sasl_continue->length - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgagroal_base64_encode((char*)proof, proof_length, &proof_base);

   status = pgagroal_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(server_ssl, socket, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(server_ssl, socket, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (msg->kind == 'E')
   {
      pgagroal_extract_error_message(msg, &error);
      if (error != NULL)
      {
         pgagroal_log_error("%s", error);
      }
      goto bad_password;
   }

   if (pgagroal_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgagroal_base64_decode(base64_server_signature, sasl_final->length - 11,
                          &server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, sasl_response->length - 26,
                        server_first_message, sasl_continue_response->length - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   free(error);
   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_log_warn("Wrong password for user: %s", username);

   free(error);
   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:

   free(error);
   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgagroal_free_copy_message(sasl_response);
   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_continue_response);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_ERROR;
}

static int
auth_query_get_password(int socket, SSL* server_ssl, char* username, char* database, char** password)
{
   int status;
   size_t size;
   char* aq = NULL;
   size_t result_size;
   char* result = NULL;
   struct message qmsg;
   struct message* tmsg = NULL;
   struct message* dmsg = NULL;

   *password = NULL;

   size = 53 + strlen(username);
   aq = malloc(size);

   memset(&qmsg, 0, sizeof(struct message));
   memset(aq, 0, size);

   pgagroal_write_byte(aq, 'Q');
   pgagroal_write_int32(aq + 1, size - 1);
   pgagroal_write_string(aq + 5, "SELECT * FROM public.pgagroal_get_password(\'");
   pgagroal_write_string(aq + 49, username);
   pgagroal_write_string(aq + 49 + strlen(username), "\');");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = aq;

   status = pgagroal_write_message(server_ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(server_ssl, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (pgagroal_extract_message('D', tmsg, &dmsg))
   {
      goto error;
   }

   result_size = dmsg->length - 11 + 1;
   result = (char*)malloc(result_size);
   memset(result, 0, result_size);
   memcpy(result, dmsg->data + 11, dmsg->length - 11);

   *password = result;

   free(aq);
   pgagroal_free_message(tmsg);
   pgagroal_free_copy_message(dmsg);

   return 0;

error:
   pgagroal_log_trace("auth_query_get_password: socket (%d) status (%d)", socket, status);

   if (tmsg->kind == 'E')
   {
      char* error = NULL;

      if (pgagroal_extract_error_message(tmsg, &error))
      {
         goto error;
      }

      pgagroal_log_error("%s in %s", error, database);
      free(error);
   }

   free(aq);
   pgagroal_free_message(tmsg);
   pgagroal_free_copy_message(dmsg);

   return 1;
}

static int
auth_query_client_md5(SSL* c_ssl, int client_fd, char* username, char* hash, int slot)
{
   int status;
   char salt[4];
   time_t start_time;
   bool non_blocking;
   char* md5_req = NULL;
   char* md5 = NULL;
   struct configuration* config;
   struct message* msg = NULL;

   config = (struct configuration*)shmem;

   salt[0] = (char)(random() & 0xFF);
   salt[1] = (char)(random() & 0xFF);
   salt[2] = (char)(random() & 0xFF);
   salt[3] = (char)(random() & 0xFF);

   status = pgagroal_write_auth_md5(c_ssl, client_fd, salt);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgagroal_socket_is_nonblocking(client_fd);
   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000L;
            nanosleep(&ts, NULL);

            goto retry;
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgagroal_socket_nonblocking(client_fd, false);
   }

   md5_req = malloc(36);
   memset(md5_req, 0, 36);
   memcpy(md5_req, hash + 3, 32);
   memcpy(md5_req + 32, &salt[0], 4);

   if (pgagroal_md5(md5_req, 36, &md5))
   {
      goto error;
   }

   if (strcmp(pgagroal_read_string(msg->data + 8), md5))
   {
      pgagroal_write_bad_password(c_ssl, client_fd, username);

      goto bad_password;
   }

   pgagroal_free_message(msg);

   free(md5_req);
   free(md5);

   return AUTH_SUCCESS;

bad_password:

   pgagroal_free_message(msg);

   free(md5_req);
   free(md5);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_free_message(msg);

   free(md5_req);
   free(md5);

   return AUTH_ERROR;
}

static int
auth_query_client_scram256(SSL* c_ssl, int client_fd, char* username, char* shadow, int slot)
{
   int status;
   time_t start_time;
   bool non_blocking;
   char* scram256 = NULL;
   char* s1 = NULL;
   char* s2 = NULL;
   char* s_iterations = NULL;
   char* base64_stored_key = NULL;
   char* base64_server_key = NULL;
   int iterations = 4096;
   char* stored_key = NULL;
   int stored_key_length = 0;
   char* server_key = NULL;
   int server_key_length = 0;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char* client_final_message_without_proof = NULL;
   char* client_nounce = NULL;
   char* server_nounce = NULL;
   char* salt = NULL;
   int salt_length = 0;
   char* base64_salt = NULL;
   char* base64_client_proof = NULL;
   char* client_proof_received = NULL;
   int client_proof_received_length = 0;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length = 0;
   char* base64_server_signature_calc = NULL;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_final = NULL;

   pgagroal_log_debug("auth_query_client_scram256 %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgagroal_write_auth_scram256(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgagroal_socket_is_nonblocking(client_fd);
   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000L;
            nanosleep(&ts, NULL);

            goto retry;
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgagroal_socket_nonblocking(client_fd, false);
   }

   /* Split shadow */
   scram256 = strtok(shadow, "$");
   s1 = strtok(NULL, "$");
   s2 = strtok(NULL, "$");

   s_iterations = strtok(s1, ":");
   base64_salt = strtok(NULL, ":");

   base64_stored_key = strtok(s2, ":");
   base64_server_key = strtok(NULL, ":");

   if (strcmp("SCRAM-SHA-256", scram256) != 0)
   {
      goto error;
   }

   /* Process shadow information */
   iterations = atoi(s_iterations);
   if (pgagroal_base64_decode(base64_salt, strlen(base64_salt), &salt, &salt_length))
   {
      goto error;
   }
   if (pgagroal_base64_decode(base64_stored_key, strlen(base64_stored_key), &stored_key, &stored_key_length))
   {
      goto error;
   }
   if (pgagroal_base64_decode(base64_server_key, strlen(base64_server_key), &server_key, &server_key_length))
   {
      goto error;
   }

   /* Start the flow */
   client_first_message_bare = malloc(msg->length - 25);
   memset(client_first_message_bare, 0, msg->length - 25);
   memcpy(client_first_message_bare, msg->data + 26, msg->length - 26);

   get_scram_attribute('r', (char*)msg->data + 26, msg->length - 26, &client_nounce);
   generate_nounce(&server_nounce);

   server_first_message = malloc(89);
   memset(server_first_message, 0, 89);
   snprintf(server_first_message, 89, "r=%s%s,s=%s,i=%d", client_nounce, server_nounce, base64_salt, iterations);

   status = pgagroal_create_auth_scram256_continue(client_nounce, server_nounce, base64_salt, &sasl_continue);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(c_ssl, client_fd, sasl_continue);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_scram_attribute('p', (char*)msg->data + 5, msg->length - 5, &base64_client_proof);
   pgagroal_base64_decode(base64_client_proof, strlen(base64_client_proof), &client_proof_received, &client_proof_received_length);

   client_final_message_without_proof = malloc(58);
   memset(client_final_message_without_proof, 0, 58);
   memcpy(client_final_message_without_proof, msg->data + 5, 57);

   if (verify_client_proof(stored_key, stored_key_length,
                           client_proof_received, client_proof_received_length,
                           salt, salt_length, iterations,
                           client_first_message_bare, strlen(client_first_message_bare),
                           server_first_message, strlen(server_first_message),
                           client_final_message_without_proof, strlen(client_final_message_without_proof)))
   {
      goto bad_password;
   }

   if (server_signature(NULL, salt, salt_length, iterations,
                        server_key, server_key_length,
                        client_first_message_bare, strlen(client_first_message_bare),
                        server_first_message, strlen(server_first_message),
                        client_final_message_without_proof, strlen(client_final_message_without_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   pgagroal_base64_encode((char*)server_signature_calc, server_signature_calc_length, &base64_server_signature_calc);

   status = pgagroal_create_auth_scram256_final(base64_server_signature_calc, &sasl_final);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(c_ssl, client_fd, sasl_final);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgagroal_log_debug("auth_query_client_scram256 success (%d)", slot);

   free(salt);
   free(stored_key);
   free(server_key);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(base64_client_proof);
   free(client_proof_received);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:
   pgagroal_log_debug("auth_query_client_scram256 bad_password (%d)", slot);

   free(salt);
   free(stored_key);
   free(server_key);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(base64_client_proof);
   free(client_proof_received);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:
   pgagroal_log_debug("auth_query_client_scram256 error (%d)", slot);

   free(salt);
   free(stored_key);
   free(server_key);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(base64_client_proof);
   free(client_proof_received);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgagroal_free_copy_message(sasl_continue);
   pgagroal_free_copy_message(sasl_final);

   return AUTH_ERROR;
}

static int
establish_client_tls_connection(int server, int fd, SSL** ssl)
{
   bool use_ssl = false;
   struct configuration* config = NULL;
   struct message* ssl_msg = NULL;
   struct message* msg = NULL;
   int status = -1;

   config = (struct configuration*)shmem;

   use_ssl = config->servers[server].tls;

   if (use_ssl)
   {
      status = pgagroal_create_ssl_message(&ssl_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      status = pgagroal_write_message(NULL, fd, ssl_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      status = pgagroal_read_block_message(NULL, fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (msg->kind == 'S')
      {
         create_client_tls_connection(fd, ssl);
      }
   }

   pgagroal_free_copy_message(ssl_msg);
   pgagroal_free_message(msg);

   return AUTH_SUCCESS;

error:

   pgagroal_free_copy_message(ssl_msg);
   pgagroal_free_message(msg);

   return AUTH_ERROR;
}

static int
create_client_tls_connection(int fd, SSL** ssl)
{
   SSL_CTX* ctx = NULL;
   SSL* s = NULL;
   int status = -1;

   /* We are acting as a client against the server */
   if (create_ssl_ctx(true, &ctx))
   {
      pgagroal_log_error("CTX failed");
      goto error;
   }

   /* Create SSL structure */
   if (create_ssl_client(ctx, NULL, NULL, NULL, fd, &s))
   {
      pgagroal_log_error("Client failed");
      goto error;
   }

   do
   {
      status = SSL_connect(s);

      if (status != 1)
      {
         int err = SSL_get_error(s, status);
         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
               break;
            case SSL_ERROR_SYSCALL:
               pgagroal_log_error("SSL_ERROR_SYSCALL: FD %d", fd);
               pgagroal_log_error("%s", ERR_error_string(err, NULL));
               pgagroal_log_error("%s", ERR_lib_error_string(err));
               pgagroal_log_error("%s", ERR_reason_error_string(err));
               errno = 0;
               goto error;
               break;
            case SSL_ERROR_SSL:
               pgagroal_log_error("SSL_ERROR_SSL: FD %d", fd);
               pgagroal_log_error("%s", ERR_error_string(err, NULL));
               pgagroal_log_error("%s", ERR_lib_error_string(err));
               pgagroal_log_error("%s", ERR_reason_error_string(err));
               errno = 0;
               goto error;
               break;
         }
         ERR_clear_error();
      }
   }
   while (status != 1);

   *ssl = s;

   return AUTH_SUCCESS;

error:

   *ssl = s;

   return AUTH_ERROR;
}
