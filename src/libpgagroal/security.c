/*
 * Copyright (C) 2020 Red Hat
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
#include <message.h>
#include <network.h>
#include <pool.h>
#include <security.h>
#include <server.h>
#include <utils.h>

#define ZF_LOG_TAG "security"
#include <zf_log.h>

/* system */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <sys/types.h>

static int get_auth_type(struct message* msg, int* auth_type);
static int compare_auth_response(struct message* orig, struct message* response, int auth_type);

static int use_pooled_connection(SSL* c_ssl, int client_fd, int slot, char* username, int hba_method, void* shmem);
static int use_unpooled_connection(struct message* msg, SSL* c_ssl, int client_fd, int slot,
                                   char* username, int hba_method, void* shmem);
static int client_trust(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem);
static int client_password(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem);
static int client_md5(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem);
static int client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem);
static int client_ok(SSL* c_ssl, int client_fd, int auth_method, int slot, void* shmem);
static int server_passthrough(struct message* msg, int auth_type, SSL* c_ssl, int client_fd, int slot, void* shmem);
static int server_authenticate(struct message* msg, int auth_type, char* username, char* password,
                               int slot, void* shmem);
static int server_trust(int slot, void* shmem);
static int server_password(char* username, char* password, int slot, void* shmem);
static int server_md5(char* username, char* password, int slot, void* shmem);
static int server_scram256(char* username, char* password, int slot, void* shmem);

static bool is_allowed(char* username, char* database, char* address, void* shmem, int* hba_method);
static bool is_allowed_username(char* username, char* entry);
static bool is_allowed_database(char* database, char* entry);
static bool is_allowed_address(char* address, char* entry);
static bool is_disabled(char* database, void* shmem);

static int   get_hba_method(int index, void* shmem);
static char* get_password(char* username, void* shmem);
static int   get_salt(void* data, char** salt);

static int derive_key_iv(char* password, unsigned char* key, unsigned char* iv);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext);

static int sasl_prep(char* password, char** password_prep);
static int generate_nounce(char** nounce);
static int get_scram_attribute(char attribute, char* input, char** value);
static int client_proof(char* password, char* salt, int salt_length, int iterations,
                        char* client_first_message_bare, char* server_first_message, char* client_final_message_wo_proof,
                        unsigned char** result, int* result_length);
static int  salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length);
static int  salted_password_key(unsigned char* salted_password, int salted_password_length, char* key,
                                unsigned char** result, int* result_length);
static int  stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length);
static int  generate_salt(char** salt, int* size);
static int  server_signature(char* password, char* salt, int salt_length, int iterations,
                             char* client_first_message_bare, char* server_first_message, char* client_final_message_wo_proof,
                             unsigned char** result, int* result_length);

static bool is_tls_user(char* username, char* database, void* shmem);
static int  create_ssl_ctx(bool client, SSL_CTX** ctx);
static int  create_ssl_server(SSL_CTX* ctx, int socket, void* shmem, SSL** ssl);

int
pgagroal_authenticate(int client_fd, char* address, void* shmem, int* slot, SSL** client_ssl)
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
   SSL* c_ssl = NULL;

   config = (struct configuration*)shmem;

   *slot = -1;
   *client_ssl = NULL;

   /* Receive client calls - at any point if client exits return AUTH_ERROR */
   status = pgagroal_read_block_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   request = pgagroal_get_request(msg);

   /* Cancel request: 80877102 */
   if (request == 80877102)
   {
      ZF_LOGD("Cancel request from client: %d", client_fd);

      /* We need to find the server for the connection */
      pgagroal_get_primary(shmem, &server);

      if (pgagroal_connect(shmem, config->servers[server].host, config->servers[server].port, &server_fd))
      {
         ZF_LOGE("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
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
      ZF_LOGD("GSS request from client: %d", client_fd);
      status = pgagroal_write_notice(NULL, client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      status = pgagroal_read_block_message(NULL, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      request = pgagroal_get_request(msg);
   }

   /* SSL request: 80877103 */
   if (request == 80877103)
   {
      ZF_LOGD("SSL request from client: %d", client_fd);

      if (config->tls)
      {
         SSL_CTX* ctx = NULL;

         /* We are acting as a server against the client */
         if (create_ssl_ctx(false, &ctx))
         {
            goto error;
         }

         if (create_ssl_server(ctx, client_fd, shmem, &c_ssl))
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
            ZF_LOGE("SSL failed: %d", status);
            goto error;
         }

         status = pgagroal_read_block_message(c_ssl, client_fd, &msg);
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

         status = pgagroal_read_block_message(NULL, client_fd, &msg);
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
      ZF_LOGV("authenticate: username/database (%d)", client_fd);
      pgagroal_extract_username_database(request_msg, &username, &database);

      /* TLS scenario */
      if (is_tls_user(username, database, shmem) && c_ssl == NULL)
      {
         ZF_LOGD("authenticate: tls: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Verify client against pgagroal_hba.conf */
      if (!is_allowed(username, database, address, shmem, &hba_method))
      {
         /* User not allowed */
         ZF_LOGD("authenticate: not allowed: %s / %s / %s", username, database, address);
         pgagroal_write_no_hba_entry(c_ssl, client_fd, username, database, address);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Reject scenario */
      if (hba_method == SECURITY_REJECT)
      {
         ZF_LOGD("authenticate: reject: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Gracefully scenario */
      if (config->gracefully)
      {
         ZF_LOGD("authenticate: gracefully: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Disabled scenario */
      if (is_disabled(database, shmem))
      {
         ZF_LOGD("authenticate: disabled: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(c_ssl, client_fd);
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      /* Get connection */
      ret = pgagroal_get_connection(shmem, username, database, true, slot);
      if (ret != 0)
      {
         if (ret == 1)
         {
            /* Pool full */
            ZF_LOGD("authenticate: pool is full");
            pgagroal_write_pool_full(c_ssl, client_fd);
         }
         else
         {
            /* Other error */
            ZF_LOGD("authenticate: connection error");
            pgagroal_write_connection_refused(c_ssl, client_fd);
         }
         pgagroal_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      if (config->connections[*slot].has_security != SECURITY_INVALID)
      {
         ZF_LOGD("authenticate: getting pooled connection");
         pgagroal_free_message(msg);

         ret = use_pooled_connection(c_ssl, client_fd, *slot, username, hba_method, shmem);
         if (ret == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (ret == AUTH_ERROR)
         {
            goto error;
         }

         ZF_LOGD("authenticate: got pooled connection (%d)", *slot);
      }
      else
      {
         ZF_LOGD("authenticate: creating pooled connection");

         ret = use_unpooled_connection(request_msg, c_ssl, client_fd, *slot, username, hba_method, shmem);
         if (ret == AUTH_BAD_PASSWORD)
         {
            goto bad_password;
         }
         else if (ret == AUTH_ERROR)
         {
            goto error;
         }

         ZF_LOGD("authenticate: created pooled connection (%d)", *slot);
      }

      pgagroal_free_copy_message(request_msg);
      free(username);
      free(database);
      
      ZF_LOGD("authenticate: SUCCESS");
      return AUTH_SUCCESS;
   }
   else if (request == -1)
   {
      goto error;
   }
   else
   {
      ZF_LOGD("authenticate: old version: %d (%s)", request, address);
      pgagroal_write_connection_refused_old(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto bad_password;
   }

bad_password:
   pgagroal_free_message(msg);
   pgagroal_free_copy_message(request_msg);

   free(username);
   free(database);

   ZF_LOGD("authenticate: BAD_PASSWORD");
   return AUTH_BAD_PASSWORD;

error:
   pgagroal_free_message(msg);
   pgagroal_free_copy_message(request_msg);

   free(username);
   free(database);

   ZF_LOGD("authenticate: ERROR");
   return AUTH_ERROR;
}

int
pgagroal_prefill_auth(char* username, char* password, char* database, void* shmem, int* slot)
{
   int server_fd = -1;
   int auth_type = -1;
   struct configuration* config = NULL;
   struct message* startup_msg = NULL;
   struct message* msg = NULL;
   int ret = -1;
   int status = -1;

   config = (struct configuration*)shmem;

   /* Get connection */
   ret = pgagroal_get_connection(shmem, username, database, false, slot);
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

   status = pgagroal_write_message(NULL, server_fd, startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(NULL, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_auth_type(msg, &auth_type);
   ZF_LOGV("prefill_auth: auth type %d", auth_type);

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

   if (server_authenticate(msg, auth_type, username, password, *slot, shmem))
   {
      goto error;
   }

   if (config->servers[config->connections[*slot].server].primary == SERVER_NOTINIT ||
       config->servers[config->connections[*slot].server].primary == SERVER_NOTINIT_PRIMARY)
   {
      ZF_LOGD("Verify server mode: %d", config->connections[*slot].server);
      pgagroal_update_server_state(shmem, *slot, server_fd);
      pgagroal_server_status(shmem);
   }

   ZF_LOGV("prefill_auth: has_security %d", config->connections[*slot].has_security);
   ZF_LOGD("prefill_auth: SUCCESS");

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_message(msg);

   return AUTH_SUCCESS;

error:

   ZF_LOGD("authenticate: ERROR");

   if (*slot != -1)
   {
      pgagroal_kill_connection(shmem, *slot);
   }

   *slot = -1;

   pgagroal_free_copy_message(startup_msg);
   pgagroal_free_message(msg);

   return AUTH_ERROR;
}

static int
get_auth_type(struct message* msg, int* auth_type)
{
   int32_t length;
   int32_t type = -1;
   int offset;

   if (msg->kind != 'R')
   {
      return 1;
   }

   length = pgagroal_read_int32(msg->data + 1);
   type = pgagroal_read_int32(msg->data + 5);
   offset = 9;

   switch (type)
   {
      case 0:
         ZF_LOGV("Backend: R - Success");
         break;
      case 2:
         ZF_LOGV("Backend: R - KerberosV5");
         break;
      case 3:
         ZF_LOGV("Backend: R - CleartextPassword");
         break;
      case 5:
         ZF_LOGV("Backend: R - MD5Password");
         ZF_LOGV("             Salt %02hhx%02hhx%02hhx%02hhx",
                 (signed char)(pgagroal_read_byte(msg->data + 9) & 0xFF),
                 (signed char)(pgagroal_read_byte(msg->data + 10) & 0xFF),
                 (signed char)(pgagroal_read_byte(msg->data + 11) & 0xFF),
                 (signed char)(pgagroal_read_byte(msg->data + 12) & 0xFF));
         break;
      case 6:
         ZF_LOGV("Backend: R - SCMCredential");
         break;
      case 7:
         ZF_LOGV("Backend: R - GSS");
         break;
      case 8:
         ZF_LOGV("Backend: R - GSSContinue");
         break;
      case 9:
         ZF_LOGV("Backend: R - SSPI");
         break;
      case 10:
         ZF_LOGV("Backend: R - SASL");
         while (offset < length - 8)
         {
            char* mechanism = pgagroal_read_string(msg->data + offset);
            ZF_LOGV("             %s", mechanism);
            offset += strlen(mechanism) + 1;
         }
         break;
      case 11:
         ZF_LOGV("Backend: R - SASLContinue");
         break;
      case 12:
         ZF_LOGV("Backend: R - SASLFinal");
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
use_pooled_connection(SSL* c_ssl, int client_fd, int slot, char* username, int hba_method, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   struct configuration* config = NULL;
   struct message* auth_msg = NULL;
   struct message* msg = NULL;
   char* password = NULL;

   config = (struct configuration*)shmem;

   password = get_password(username, shmem);

   if (hba_method == SECURITY_ALL)
   {
      hba_method = config->connections[slot].has_security;
   }

   if (password == NULL)
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
         status = pgagroal_read_block_message(c_ssl, client_fd, &msg);
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
         client_trust(c_ssl, client_fd, username, password, slot, shmem);
      }
      else if (hba_method == SECURITY_PASSWORD)
      {
         /* R/3 */
         status = client_password(c_ssl, client_fd, username, password, slot, shmem);
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
         status = client_md5(c_ssl, client_fd, username, password, slot, shmem);
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
         status = client_scram256(c_ssl, client_fd, username, password, slot, shmem);
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

      if (client_ok(c_ssl, client_fd, hba_method, slot, shmem))
      {
         goto error;
      }
   }

   return AUTH_SUCCESS;

bad_password:

   ZF_LOGV("use_pooled_connection: bad password for slot %d", slot);

   return AUTH_BAD_PASSWORD;

error:

   ZF_LOGV("use_pooled_connection: failed for slot %d", slot);

   return AUTH_ERROR;
}

static int
use_unpooled_connection(struct message* request_msg, SSL* c_ssl, int client_fd, int slot,
                        char* username, int hba_method, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   int server_fd;
   int auth_type = -1;
   char* password;
   struct message* msg = NULL;
   struct message* auth_msg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   password = get_password(username, shmem);

   /* Disallow unknown users */
   if (password == NULL && !config->allow_unknown_users)
   {
      ZF_LOGD("reject: %s", username);
      pgagroal_write_connection_refused(c_ssl, client_fd);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }

   /* Send auth request to PostgreSQL */
   ZF_LOGV("authenticate: client auth request (%d)", client_fd);
   status = pgagroal_write_message(NULL, server_fd, request_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(msg);

   /* Keep response, and send response to client */
   ZF_LOGV("authenticate: server auth request (%d)", server_fd);
   status = pgagroal_read_block_message(NULL, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_auth_type(msg, &auth_type);
   ZF_LOGV("authenticate: auth type %d", auth_type);

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
      ZF_LOGI("Unsupported security model: %d", auth_type);
      pgagroal_write_unsupported_security_model(c_ssl, client_fd, username);
      pgagroal_write_empty(c_ssl, client_fd);
      goto error;
   }

   if (password == NULL)
   {
      if (server_passthrough(msg, auth_type, c_ssl, client_fd, slot, shmem))
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
         client_trust(c_ssl, client_fd, username, password, slot, shmem);
      }
      else if (hba_method == SECURITY_PASSWORD)
      {
         /* R/3 */
         status = client_password(c_ssl, client_fd, username, password, slot, shmem);
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
         status = client_md5(c_ssl, client_fd, username, password, slot, shmem);
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
         status = client_scram256(c_ssl, client_fd, username, password, slot, shmem);
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

      if (server_authenticate(auth_msg, auth_type, username, password, slot, shmem))
      {
         if (pgagroal_socket_isvalid(client_fd))
         {
            pgagroal_write_connection_refused(c_ssl, client_fd);
            pgagroal_write_empty(c_ssl, client_fd);
         }

         goto error;
      }

      if (client_ok(c_ssl, client_fd, hba_method, slot, shmem))
      {
         goto error;
      }
   }

   if (config->servers[config->connections[slot].server].primary == SERVER_NOTINIT ||
       config->servers[config->connections[slot].server].primary == SERVER_NOTINIT_PRIMARY)
   {
      ZF_LOGD("Verify server mode: %d", config->connections[slot].server);
      pgagroal_update_server_state(shmem, slot, server_fd);
      pgagroal_server_status(shmem);
   }

   ZF_LOGV("authenticate: has_security %d", config->connections[slot].has_security);

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

   ZF_LOGV("use_unpooled_connection: failed for slot %d", slot);

   return AUTH_ERROR;
}

static int
client_trust(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem)
{
   ZF_LOGD("client_trust %d %d", client_fd, slot);

   return AUTH_SUCCESS;
}

static int
client_password(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem)
{
   int status;
   time_t start_time;
   struct configuration* config;
   struct message* msg = NULL;

   ZF_LOGD("client_password %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgagroal_write_auth_password(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

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

   if (!config->non_blocking)
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
client_md5(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem)
{
   int status;
   char salt[4];
   time_t start_time;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   struct configuration* config;
   struct message* msg = NULL;

   ZF_LOGD("client_md5 %d %d", client_fd, slot);

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

   if (!config->non_blocking)
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
client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot, void* shmem)
{
   int status;
   time_t start_time;
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

   ZF_LOGD("client_scram256 %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgagroal_write_auth_scram256(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

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

   if (!config->non_blocking)
   {
      pgagroal_socket_nonblocking(client_fd, false);
   }

   client_first_message_bare = malloc(msg->length - 25);
   memset(client_first_message_bare, 0, msg->length - 25);
   memcpy(client_first_message_bare, msg->data + 26, msg->length - 26);
   
   get_scram_attribute('r', (char*)msg->data + 26, &client_nounce);
   generate_nounce(&server_nounce);
   generate_salt(&salt, &salt_length);
   pgagroal_base64_encode(salt, salt_length, &base64_salt);

   server_first_message = malloc(89);
   memset(server_first_message, 0, 89);
   snprintf(server_first_message, 89, "r=%s%s,s=%s,i=4096", client_nounce, server_nounce, base64_salt);

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

   status = pgagroal_read_message(c_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_scram_attribute('p', (char*)msg->data + 5, &base64_client_proof);
   pgagroal_base64_decode(base64_client_proof, &client_proof_received, &client_proof_received_length);

   client_final_message_without_proof = malloc(58);
   memset(client_final_message_without_proof, 0, 58);
   memcpy(client_final_message_without_proof, msg->data + 5, 57);

   sasl_prep(password, &password_prep);

   if (client_proof(password_prep, salt, salt_length, 4096,
                    client_first_message_bare, server_first_message, client_final_message_without_proof,
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
                        client_first_message_bare, server_first_message, client_final_message_without_proof,
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

   ZF_LOGD("client_scram256 done");

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
client_ok(SSL* c_ssl, int client_fd, int auth_method, int slot, void* shmem)
{
   int status;
   size_t size;
   char* data;
   struct message msg;
   struct configuration* config;

   data = NULL;
   memset(&msg, 0, sizeof(msg));

   config = (struct configuration*)shmem;

   if (auth_method == SECURITY_TRUST)
   {
      size = config->connections[slot].security_lengths[0];
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[0], size);
   }
   else if (auth_method == SECURITY_PASSWORD || auth_method == SECURITY_MD5)
   {
      size = config->connections[slot].security_lengths[2];
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[2], size);
   }
   else if (auth_method == SECURITY_SCRAM256)
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
server_passthrough(struct message* msg, int auth_type, SSL* c_ssl, int client_fd, int slot, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   int server_fd;
   int auth_index = 0;
   int auth_response = -1;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   ZF_LOGV("server_passthrough %d %d", auth_type, slot);

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      ZF_LOGE("Security message too large: %ld", msg->length);
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
         ZF_LOGE("Security message too large: %ld", msg->length);
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
            ZF_LOGE("Security message too large: %ld", msg->length);
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

         status = pgagroal_read_block_message(c_ssl, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         if (msg->length > SECURITY_BUFFER_SIZE)
         {
            ZF_LOGE("Security message too large: %ld", msg->length);
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
      ZF_LOGV("authenticate: auth response %d", auth_response);

      if (auth_response == 0)
      {
         if (msg->length > SECURITY_BUFFER_SIZE)
         {
            ZF_LOGE("Security message too large: %ld", msg->length);
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

   return 0;

error:

   return 1;
}

static int
server_authenticate(struct message* msg, int auth_type, char* username, char* password, int slot, void* shmem)
{
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      ZF_LOGE("Security message too large: %ld", msg->length);
      goto error;
   }

   config->connections[slot].security_lengths[0] = msg->length;
   memcpy(&config->connections[slot].security_messages[0], msg->data, msg->length);

   if (auth_type == SECURITY_TRUST)
   {
      return server_trust(slot, shmem);
   }
   else if (auth_type == SECURITY_PASSWORD)
   {
      return server_password(username, password, slot, shmem);
   }
   else if (auth_type == SECURITY_MD5)
   {
      return server_md5(username, password, slot, shmem);
   }
   else if (auth_type == SECURITY_SCRAM256)
   {
      return server_scram256(username, password, slot, shmem);
   }

error:

   ZF_LOGE("server_authenticate: %d", auth_type);

   return AUTH_ERROR;
}

static int
server_trust(int slot, void* shmem)
{
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   ZF_LOGV("server_trust");

   config->connections[slot].has_security = SECURITY_TRUST;

   return AUTH_SUCCESS;
}

static int
server_password(char* username, char* password, int slot, void* shmem)
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

   ZF_LOGV("server_password");

   status = pgagroal_create_auth_password_response(password, &password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_write_message(NULL, server_fd, password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = password_msg->length;
   memcpy(&config->connections[slot].security_messages[auth_index], password_msg->data, password_msg->length);
   auth_index++;

   status = pgagroal_read_block_message(NULL, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      ZF_LOGE("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   ZF_LOGV("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         ZF_LOGE("Security message too large: %ld", auth_msg->length);
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

   ZF_LOGW("Wrong password for user: %s", username);

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_BAD_PASSWORD;

error:

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_message(auth_msg);

   return AUTH_ERROR;
}

static int
server_md5(char* username, char* password, int slot, void* shmem)
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

   ZF_LOGV("server_md5");

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

   status = pgagroal_write_message(NULL, server_fd, md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = md5_msg->length;
   memcpy(&config->connections[slot].security_messages[auth_index], md5_msg->data, md5_msg->length);
   auth_index++;

   status = pgagroal_read_block_message(NULL, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      ZF_LOGE("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   ZF_LOGV("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         ZF_LOGE("Security message too large: %ld", auth_msg->length);
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

   ZF_LOGW("Wrong password for user: %s", username);

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
server_scram256(char* username, char* password, int slot, void* shmem)
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
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   server_fd = config->connections[slot].fd;

   ZF_LOGV("server_scram256");

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

   status = pgagroal_write_message(NULL, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(NULL, server_fd, &sasl_continue);
   if (sasl_continue->length > SECURITY_BUFFER_SIZE)
   {
      ZF_LOGE("Security message too large: %ld", sasl_continue->length);
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = sasl_continue->length;
   memcpy(&config->connections[slot].security_messages[auth_index], sasl_continue->data, sasl_continue->length);
   auth_index++;

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), &err);

   if (err != NULL)
   {
      ZF_LOGE("SCRAM-SHA-256: %s", err);
      goto error;
   }

   pgagroal_base64_decode(base64_salt, &salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = config->connections[slot].security_messages[1] + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = config->connections[slot].security_messages[2] + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, server_first_message, &wo_proof[0],
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

   status = pgagroal_write_message(NULL, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(NULL, server_fd, &sasl_final);
   if (sasl_final->length > SECURITY_BUFFER_SIZE)
   {
      ZF_LOGE("Security message too large: %ld", sasl_final->length);
      goto error;
   }

   config->connections[slot].security_lengths[auth_index] = sasl_final->length;
   memcpy(&config->connections[slot].security_messages[auth_index], sasl_final->data, sasl_final->length);
   auth_index++;

   /* Get 'v' attribute */
   base64_server_signature = config->connections[slot].security_messages[4] + 11;
   pgagroal_base64_decode(base64_server_signature, &server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        client_first_message_bare, server_first_message, &wo_proof[0],
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
   pgagroal_free_copy_message(sasl_continue_response);

   return AUTH_SUCCESS;

bad_password:

   ZF_LOGW("Wrong password for user: %s", username);

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
   pgagroal_free_copy_message(sasl_continue_response);

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
   pgagroal_free_copy_message(sasl_continue_response);

   return AUTH_ERROR;
}

static bool
is_allowed(char* username, char* database, char* address, void* shmem, int* hba_method)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_hbas; i++)
   {
      if (is_allowed_address(address, config->hbas[i].address) &&
          is_allowed_database(database, config->hbas[i].database) &&
          is_allowed_username(username, config->hbas[i].username))
      {
         *hba_method = get_hba_method(i, shmem);

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
      ZF_LOGW("Invalid HBA entry: %s", entry);
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
         ZF_LOGW("Invalid HBA entry: %s", entry);
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
         ZF_LOGW("Invalid HBA entry: %s", entry);
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
is_disabled(char* database, void* shmem)
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
get_hba_method(int index, void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!strcasecmp(config->hbas[index].method, "reject"))
      return SECURITY_REJECT;

   if (!strcasecmp(config->hbas[index].method, "trust"))
      return SECURITY_TRUST;

   if (!strcasecmp(config->hbas[index].method, "password"))
      return SECURITY_PASSWORD;

   if (!strcasecmp(config->hbas[index].method, "md5"))
      return SECURITY_MD5;

   if (!strcasecmp(config->hbas[index].method, "scram-sha-256"))
      return SECURITY_SCRAM256;

   if (!strcasecmp(config->hbas[index].method, "all"))
      return SECURITY_ALL;

   return SECURITY_REJECT;
}

static char*
get_password(char* username, void* shmem)
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

   memset(&line, 0, sizeof(line));
   if (fgets(line, sizeof(line), master_key_file) == NULL)
   {
      goto error;
   }

   pgagroal_base64_decode(&line[0], &mk, &mk_length);

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
   char *out;

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
pgagroal_user_known(char* user, void* shmem)
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
pgagroal_tls_valid(void* shmem)
{
   struct configuration* config;
   struct stat st = {0};

   config = (struct configuration*)shmem;

   if (config->tls)
   {
      if (strlen(config->tls_cert_file) == 0)
      {
         ZF_LOGE("No TLS certificate defined");
         goto error;
      }

      if (strlen(config->tls_key_file) == 0)
      {
         ZF_LOGE("No TLS private key defined");
         goto error;
      }

      if (stat(config->tls_cert_file, &st) == -1)
      {
         ZF_LOGE("Can't locate TLS certificate file: %s", config->tls_cert_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         ZF_LOGE("TLS certificate file is not a regular file: %s", config->tls_cert_file);
         goto error;
      }

      if (st.st_uid != geteuid())
      {
         ZF_LOGE("TLS certificate file not owned by user: %s", config->tls_cert_file);
         goto error;
      }

      memset(&st, 0, sizeof(struct stat));

      if (stat(config->tls_key_file, &st) == -1)
      {
         ZF_LOGE("Can't locate TLS private key file: %s", config->tls_key_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         ZF_LOGE("TLS private key file is not a regular file: %s", config->tls_key_file);
         goto error;
      }

      if (st.st_uid != geteuid())
      {
         ZF_LOGE("TLS private key file not owned by user: %s", config->tls_key_file);
         goto error;
      }

      if (st.st_mode & (S_IRWXG | S_IRWXO))
      {
         ZF_LOGE("TLS private key file must have 0600 permissions: %s", config->tls_key_file);
         goto error;
      }

      if (strlen(config->tls_ca_file) > 0)
      {
         memset(&st, 0, sizeof(struct stat));

         if (stat(config->tls_ca_file, &st) == -1)
         {
            ZF_LOGE("Can't locate TLS CA file: %s", config->tls_ca_file);
            goto error;
         }

         if (!S_ISREG(st.st_mode))
         {
            ZF_LOGE("TLS CA file is not a regular file: %s", config->tls_ca_file);
            goto error;
         }

         if (st.st_uid != geteuid())
         {
            ZF_LOGE("TLS CA file not owned by user: %s", config->tls_ca_file);
            goto error;
         }
      }
      else
      {
         ZF_LOGD("No TLS CA file");
      }
   }

   return 0;

error:

   return 1;
}

static int
derive_key_iv(char *password, unsigned char *key, unsigned char *iv)
{

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   OpenSSL_add_all_algorithms();
#endif

   if (!EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha1(), NULL,
                       (unsigned char *) password, strlen(password), 1,
                       key, iv))
   {
      return 1;
   }

   return 0;
}

static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length)
{
   EVP_CIPHER_CTX *ctx = NULL;
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
   EVP_CIPHER_CTX *ctx = NULL;
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
get_scram_attribute(char attribute, char* input, char** value)
{
   char* dup = NULL;
   char* result = NULL;
   char* ptr = NULL;
   size_t size;
   char match[2];

   match[0] = attribute;
   match[1] = '=';

   dup = strdup(input);
   ptr = strtok(dup, ",");
   while (ptr != NULL)
   {
      if (!strncmp(ptr, &match[0], 2))
      {
         size = strlen(ptr) - 1;
         result = malloc(size);
         memset(result, 0, size);
         memcpy(result, ptr + 2, size);
      }

      ptr = strtok(NULL, ",");
   }

   *value = result;

   free(dup);

   return 0;
}

static int
client_proof(char* password, char* salt, int salt_length, int iterations,
             char* client_first_message_bare, char* server_first_message, char* client_final_message_wo_proof,
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

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, strlen(client_first_message_bare)) != 1)
   {
      goto error;
   }
   
   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, strlen(server_first_message)) != 1)
   {
      goto error;
   }
   
   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }
   
   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, strlen(client_final_message_wo_proof)) != 1)
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

   if (HMAC_Update(ctx, (unsigned char *)salt, salt_length) != 1)
   {
      goto error;
   }
   
   if (HMAC_Update(ctx, (unsigned char *)&one, sizeof(one)) != 1)
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

   if (HMAC_Update(ctx, (unsigned char *)key, strlen(key)) != 1)
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
                 char* client_first_message_bare, char* server_first_message, char* client_final_message_wo_proof,
                 unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* s_k = NULL;
   int s_k_length;
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

   if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
   {
      goto error;
   }

   if (salted_password_key(s_p, s_p_length, "Server Key", &s_k, &s_k_length))
   {
      goto error;
   }

   /* Server signature: HMAC(ServerKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, strlen(client_first_message_bare)) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, strlen(server_first_message)) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, strlen(client_final_message_wo_proof)) != 1)
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
   free(s_k);

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
   free(s_k);

   return 1;
}

static bool
is_tls_user(char* username, char* database, void* shmem)
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
   SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_OFF);

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
create_ssl_server(SSL_CTX* ctx, int socket, void* shmem, SSL** ssl)
{
   SSL* s = NULL;
   STACK_OF(X509_NAME) *root_cert_list = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->tls_cert_file) == 0)
   {
      ZF_LOGE("No TLS certificate defined");
      goto error;
   }

   if (strlen(config->tls_key_file) == 0)
   {
      ZF_LOGE("No TLS private key defined");
      goto error;
   }

   if (SSL_CTX_use_certificate_chain_file(ctx, config->tls_cert_file) != 1)
   {
      ZF_LOGE("Couldn't load TLS certificate: %s", config->tls_cert_file);
      goto error;
   }

   if (SSL_CTX_use_PrivateKey_file(ctx, config->tls_key_file, SSL_FILETYPE_PEM) != 1)
   {
      ZF_LOGE("Couldn't load TLS private key: %s", config->tls_key_file);
      goto error;
   }

   if (SSL_CTX_check_private_key(ctx) != 1)
   {
      ZF_LOGE("TLS private key check failed: %s", config->tls_key_file);
      goto error;
   }

   if (strlen(config->tls_ca_file) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, config->tls_ca_file, NULL) != 1)
      {
         ZF_LOGE("Couldn't load TLS CA: %s", config->tls_ca_file);
         goto error;
      }
      
      root_cert_list = SSL_load_client_CA_file(config->tls_ca_file);
      if (root_cert_list == NULL)
      {
         ZF_LOGE("Couldn't load TLS CA: %s", config->tls_ca_file);
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
