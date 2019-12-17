/*
 * Copyright (C) 2019 Red Hat
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
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/md5.h>

static int get_auth_type(struct message* msg, int* auth_type);
static int compare_auth_response(struct message* orig, struct message* response, int auth_type);

static int use_pooled_connection(int client_fd, int slot, char* username, int hba_type, void* shmem);
static int use_unpooled_connection(struct message* msg, int client_fd, int server_fd, int slot, char* username, int hba_type, void* shmem);
static int client_trust(int client_fd, char* username, char* password, int slot, void* shmem);
static int client_password(int client_fd, char* username, char* password, int slot, void* shmem);
static int client_md5(int client_fd, char* username, char* password, int slot, void* shmem);
static int client_ok(int client_fd, int slot, void* shmem);
static int server_passthrough(struct message* msg, int auth_type, int client_fd, int server_fd, int slot, void* shmem);
static int server_authenticate(struct message* msg, int auth_type, char* username, char* password, int server_fd, int slot, void* shmem);

static bool is_allowed(char* username, char* database, char* address, void* shmem, int* hba_type);
static bool is_allowed_username(char* username, char* entry);
static bool is_allowed_database(char* database, char* entry);
static bool is_allowed_address(char* address, char* entry);

static int   get_hba_type(int index, void* shmem);
static char* get_password(char* username, void* shmem);
static int   get_salt(void* data, char** salt);

static int derive_key_iv(char* password, unsigned char* key, unsigned char* iv);
static int encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext);
static int decrypt(char* ciphertext, unsigned char* key, unsigned char* iv, char** plaintext);

int
pgagroal_authenticate(int client_fd, char* address, void* shmem, int* slot)
{
   int status = MESSAGE_STATUS_ERROR;
   int ret;
   int server_fd = -1;
   int hba_type;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* auth_msg = NULL;
   int32_t request;
   char* username = NULL;
   char* database = NULL;

   config = (struct configuration*)shmem;

   /* Receive client calls - at any point if client exits return AUTH_FAILURE */
   status = pgagroal_read_block_message(client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   request = pgagroal_get_request(msg);

   /* GSS request: 80877104 */
   if (request == 80877104)
   {
      ZF_LOGD("GSS request from client: %d", client_fd);
      status = pgagroal_write_notice(client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      status = pgagroal_read_block_message(client_fd, &msg);
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
      status = pgagroal_write_notice(client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      status = pgagroal_read_block_message(client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      request = pgagroal_get_request(msg);
   }

   /* 196608 -> Ok */
   if (request == 196608)
   {
      /* Extract parameters: username / database */
      ZF_LOGV("authenticate: username/database (%d)", client_fd);
      pgagroal_extract_username_database(msg, &username, &database);

      /* Verify client against pgagroal_hba.conf */
      if (!is_allowed(username, database, address, shmem, &hba_type))
      {
         /* User not allowed */
         ZF_LOGD("authenticate: not allowed: %s / %s / %s", username, database, address);
         pgagroal_write_no_hba_entry(client_fd, username, database, address);
         pgagroal_write_empty(client_fd);
         goto error;
      }

      /* Reject scenario */
      if (hba_type == SECURITY_REJECT)
      {
         ZF_LOGD("authenticate: reject: %s / %s / %s", username, database, address);
         pgagroal_write_connection_refused(client_fd);
         pgagroal_write_empty(client_fd);
         goto error;
      }

      /* Get connection */
      ret = pgagroal_get_connection(shmem, username, database, slot);
      if (ret != 0)
      {
         if (ret == 1)
         {
            /* Pool full */
            ZF_LOGD("authenticate: pool is full");
            pgagroal_write_pool_full(client_fd);
         }
         else
         {
            /* Other error */
            ZF_LOGD("authenticate: connection error");
            pgagroal_write_connection_refused(client_fd);
         }
         pgagroal_write_empty(client_fd);
         goto error;
      }
      server_fd = config->connections[*slot].fd;

      if (config->connections[*slot].has_security != SECURITY_INVALID)
      {
         ZF_LOGD("authenticate: getting pooled connection");
         pgagroal_free_message(msg);

         if (use_pooled_connection(client_fd, *slot, username, hba_type, shmem))
         {
            goto error;
         }

         ZF_LOGD("authenticate: got pooled connection (%d)", *slot);
      }
      else
      {
         ZF_LOGD("authenticate: creating pooled connection");

         if (use_unpooled_connection(msg, client_fd, server_fd, *slot, username, hba_type, shmem))
         {
            goto error;
         }

         ZF_LOGD("authenticate: created pooled connection (%d)", *slot);
      }

      free(username);
      free(database);
      
      ZF_LOGD("authenticate: SUCCESS");
      return AUTH_SUCCESS;
   }

error:
   pgagroal_free_message(msg);
   if (auth_msg)
   {
      pgagroal_free_copy_message(auth_msg);
   }

   free(username);
   free(database);

   ZF_LOGD("authenticate: FAILURE");
   return AUTH_FAILURE;
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
         ZF_LOGV_MEM(msg->data + offset, length - 8, "Message %p:", (const void *)msg->data + offset);
         break;
      case 12:
         ZF_LOGV("Backend: R - SASLFinal");
         ZF_LOGV_MEM(msg->data + offset, length - 8, "Message %p:", (const void *)msg->data + offset);
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
use_pooled_connection(int client_fd, int slot, char* username, int hba_type, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   struct configuration* config = NULL;
   struct message* auth_msg = NULL;
   struct message* msg = NULL;
   char* password = NULL;

   config = (struct configuration*)shmem;

   if (config->connections[slot].has_security == hba_type || hba_type == SECURITY_ALL)
   {
      /* pgagroal and PostgreSQL has the same access method */

      pgagroal_create_message(&config->connections[slot].security_messages[0],
                              config->connections[slot].security_lengths[0],
                              &auth_msg);

      status = pgagroal_write_message(client_fd, auth_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_copy_message(auth_msg);
      auth_msg = NULL;

      /* Password or MD5 */
      if (config->connections[slot].has_security != SECURITY_TRUST)
      {
         status = pgagroal_read_block_message(client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         pgagroal_create_message(&config->connections[slot].security_messages[1],
                                 config->connections[slot].security_lengths[1],
                                 &auth_msg);

         if (compare_auth_response(auth_msg, msg, config->connections[slot].has_security))
         {
            goto error;
         }

         pgagroal_free_copy_message(auth_msg);
         auth_msg = NULL;

         pgagroal_create_message(&config->connections[slot].security_messages[2],
                                 config->connections[slot].security_lengths[2],
                                 &auth_msg);

         status = pgagroal_write_message(client_fd, auth_msg);
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
      /* pgagroal and PostgreSQL has different access methods, so switch */

      if (hba_type == SECURITY_TRUST)
      {
         /* R/0 */
         if (client_trust(client_fd, username, password, slot, shmem))
         {
            goto error;
         }
      }
      else if (hba_type == SECURITY_PASSWORD)
      {
         /* R/3 */
         password = get_password(username, shmem);
         if (password == NULL || client_password(client_fd, username, password, slot, shmem))
         {
            if (password == NULL)
            {
               ZF_LOGI("User not defined: %s", username);
            }

            goto error;
         }
      }
      else if (hba_type == SECURITY_MD5)
      {
         /* R/5 */
         password = get_password(username, shmem);
         if (password == NULL || client_md5(client_fd, username, password, slot, shmem))
         {
            if (password == NULL)
            {
               ZF_LOGI("User not defined: %s", username);
            }

            goto error;
         }
      }
      else
      {
         goto error;
      }
   }

   return 0;

error:

   ZF_LOGV("use_pooled_connection: failed for slot %d", slot);

   return 1;
}

static int
use_unpooled_connection(struct message* msg, int client_fd, int server_fd, int slot, char* username, int hba_type, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_type = -1;
   char* password;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   /* Send auth request to PostgreSQL */
   ZF_LOGV("authenticate: client auth request (%d)", client_fd);
   status = pgagroal_write_message(server_fd, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(msg);

   /* Keep response, and send response to client */
   ZF_LOGV("authenticate: server auth request (%d)", server_fd);
   status = pgagroal_read_block_message(server_fd, &msg);
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
      pgagroal_write_message(client_fd, msg);
      pgagroal_write_empty(client_fd);
      goto error;
   }
   else if (auth_type != SECURITY_TRUST && auth_type != SECURITY_PASSWORD && auth_type != SECURITY_MD5 && auth_type != SECURITY_SCRAM256)
   {
      ZF_LOGI("Unsupported security model: %d", auth_type);
      pgagroal_write_unsupported_security_model(client_fd, username);
      pgagroal_write_empty(client_fd);
      goto error;
   }

   password = get_password(username, shmem);

   if (password == NULL || auth_type == SECURITY_SCRAM256 || auth_type == hba_type)
   {
      if (server_passthrough(msg, auth_type, client_fd, server_fd, slot, shmem))
      {
         goto error;
      }
   }
   else
   {
      if (server_authenticate(msg, auth_type, username, password, server_fd, slot, shmem))
      {
         goto error;
      }

      if (hba_type == SECURITY_TRUST)
      {
         /* R/0 */
         if (client_trust(client_fd, username, password, slot, shmem))
         {
            goto error;
         }
      }
      else if (hba_type == SECURITY_PASSWORD)
      {
         /* R/3 */
         if (client_password(client_fd, username, password, slot, shmem))
         {
            goto error;
         }
      }
      else if (hba_type == SECURITY_MD5)
      {
         /* R/5 */
         if (client_md5(client_fd, username, password, slot, shmem))
         {
            goto error;
         }
      }
      else
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

   return 0;

error:

   ZF_LOGV("use_unpooled_connection: failed for slot %d", slot);

   return 1;
}

static int
client_trust(int client_fd, char* username, char* password, int slot, void* shmem)
{
   ZF_LOGD("client_trust %d %d", client_fd, slot);

   if (client_ok(client_fd, slot, shmem))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
client_password(int client_fd, char* username, char* password, int slot, void* shmem)
{
   int status;
   time_t start_time;
   int timeout;
   struct configuration* config;
   struct message* msg = NULL;

   ZF_LOGD("client_password %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgagroal_write_auth_password(client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);
   if (config->blocking_timeout > 0)
   {
      timeout = config->blocking_timeout;
   }
   else
   {
      timeout = 10;
   }

   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(client_fd, 10, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < timeout)
      {
         /* Sleep for 100ms */
         struct timespec ts;
         ts.tv_sec = 0;
         ts.tv_nsec = 100000000L;
         nanosleep(&ts, NULL);

         goto retry;
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgagroal_socket_nonblocking(client_fd, false);

   if (strcmp(pgagroal_read_string(msg->data + 5), password))
   {
      pgagroal_write_bad_password(client_fd, username);
      pgagroal_write_empty(client_fd);

      goto error;
   }

   if (client_ok(client_fd, slot, shmem))
   {
      goto error;
   }

   pgagroal_free_message(msg);

   return 0;

error:

   pgagroal_free_message(msg);

   return 1;
}

static int
client_md5(int client_fd, char* username, char* password, int slot, void* shmem)
{
   int status;
   char salt[4];
   time_t start_time;
   int timeout;
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

   status = pgagroal_write_auth_md5(client_fd, salt);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);
   if (config->blocking_timeout > 0)
   {
      timeout = config->blocking_timeout;
   }
   else
   {
      timeout = 10;
   }

   pgagroal_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgagroal_read_timeout_message(client_fd, 10, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < timeout)
      {
         /* Sleep for 100ms */
         struct timespec ts;
         ts.tv_sec = 0;
         ts.tv_nsec = 100000000L;
         nanosleep(&ts, NULL);

         goto retry;
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgagroal_socket_nonblocking(client_fd, false);

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
      pgagroal_write_bad_password(client_fd, username);
      pgagroal_write_empty(client_fd);

      goto error;
   }

   if (client_ok(client_fd, slot, shmem))
   {
      goto error;
   }

   pgagroal_free_message(msg);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);

   return 0;

error:

   pgagroal_free_message(msg);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);

   return 1;
}

static int
client_ok(int client_fd, int slot, void* shmem)
{
   int status;
   int auth_type;
   size_t size;
   char* data;
   struct message msg;
   struct configuration* config;

   data = NULL;
   memset(&msg, 0, sizeof(msg));

   config = (struct configuration*)shmem;

   auth_type = config->connections[slot].has_security;

   if (auth_type == SECURITY_TRUST)
   {
      size = config->connections[slot].security_lengths[0];
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[0], size);
   }
   else if (auth_type == SECURITY_PASSWORD || auth_type == SECURITY_MD5)
   {
      size = config->connections[slot].security_lengths[2];
      data = malloc(size);
      memcpy(data, config->connections[slot].security_messages[2], size);
   }
   else
   {
      goto error;
   }

   msg.kind = 'R';
   msg.length = size;
   msg.data = data;

   status = pgagroal_write_message(client_fd, &msg);
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
server_passthrough(struct message* msg, int auth_type, int client_fd, int server_fd, int slot, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 0;
   int auth_response = -1;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

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

   status = pgagroal_write_message(client_fd, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(msg);

   /* Non-trust clients */
   if (auth_type != SECURITY_TRUST)
   {
      /* Receive client response, keep it, and send it to PostgreSQL */
      status = pgagroal_read_block_message(client_fd, &msg);
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

      status = pgagroal_write_message(server_fd, msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
      pgagroal_free_message(msg);

      status = pgagroal_read_block_message(server_fd, &msg);
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

         status = pgagroal_write_message(client_fd, msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = pgagroal_read_block_message(client_fd, &msg);
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

         status = pgagroal_write_message(server_fd, msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgagroal_free_message(msg);

         status = pgagroal_read_block_message(server_fd, &msg);
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

      status = pgagroal_write_message(client_fd, msg);
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
server_authenticate(struct message* msg, int auth_type, char* username, char* password, int server_fd, int slot, void* shmem)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 0;
   int auth_response = -1;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   char md5str[36];
   char* salt = NULL;
   struct message* auth_msg = NULL;
   struct message* password_msg = NULL;
   struct message* md5_msg = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   ZF_LOGV("server_authenticate %d", auth_type);

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

   /* Non-trust */
   if (auth_type == SECURITY_TRUST)
   {
      /* Trust */
      config->connections[slot].has_security = SECURITY_TRUST;
      auth_response = 0;
   }
   else if (auth_type == SECURITY_PASSWORD)
   {
      status = pgagroal_create_auth_password_response(password, &password_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      status = pgagroal_write_message(server_fd, password_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      config->connections[slot].security_lengths[auth_index] = password_msg->length;
      memcpy(&config->connections[slot].security_messages[auth_index], password_msg->data, password_msg->length);
      auth_index++;

      status = pgagroal_read_block_message(server_fd, &auth_msg);

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

         config->connections[slot].has_security = auth_type;
      }
   }
   else if (auth_type == SECURITY_MD5)
   {
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

      ZF_LOGV_MEM(&config->connections[slot].security_messages[0], config->connections[slot].security_lengths[0],
                  "                      Message %p:", (const void *)&config->connections[slot].security_messages[0]);


      memset(&md5str, 0, sizeof(md5str));
      snprintf(&md5str[0], 36, "md5%s", md5);

      status = pgagroal_create_auth_md5_response(md5str, &md5_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      status = pgagroal_write_message(server_fd, md5_msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      config->connections[slot].security_lengths[auth_index] = md5_msg->length;
      memcpy(&config->connections[slot].security_messages[auth_index], md5_msg->data, md5_msg->length);
      auth_index++;

      status = pgagroal_read_block_message(server_fd, &auth_msg);

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

         config->connections[slot].has_security = auth_type;
      }
   }

   if (auth_response != 0)
   {
      goto error;
   }

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return 0;

error:

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgagroal_free_copy_message(password_msg);
   pgagroal_free_copy_message(md5_msg);
   pgagroal_free_message(auth_msg);

   return 1;
}

static bool
is_allowed(char* username, char* database, char* address, void* shmem, int* hba_type)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_hbas; i++)
   {
      if (is_allowed_address(address, config->hbas[i].address) &&
          is_allowed_database(database, config->hbas[i].database) &&
          is_allowed_username(username, config->hbas[i].username))
      {
         *hba_type = get_hba_type(i, shmem);

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

static int
get_hba_type(int index, void* shmem)
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

   pgagroal_base64_decode(&line[0], &mk);

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
pgagroal_encrypt(char* plaintext, char* password, char** ciphertext)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv) != 0)
   {
      return 1;
   }

   return encrypt(plaintext, key, iv, ciphertext);
}

int
pgagroal_decrypt(char* ciphertext, char* password, char** plaintext)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv) != 0)
   {
      return 1;
   }

   return decrypt(ciphertext, key, iv, plaintext);
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
encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext)
{
   EVP_CIPHER_CTX *ctx = NULL;
   int length;
   size_t size;
   char* ct = NULL;

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
                         (unsigned char*)ct, &length,
                         (unsigned char*)plaintext, strlen(plaintext)) != 1)
   {
      goto error;
   }

   if (EVP_EncryptFinal_ex(ctx, (unsigned char*)ct + length, &length) != 1)
   {
      goto error;
   }

   EVP_CIPHER_CTX_free(ctx);

   *ciphertext = ct;

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
decrypt(char* ciphertext, unsigned char* key, unsigned char* iv, char** plaintext)
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

   size = strlen(ciphertext) + EVP_CIPHER_block_size(EVP_aes_256_cbc());
   pt = malloc(size);
   memset(pt, 0, size);

   if (EVP_DecryptUpdate(ctx,
                         (unsigned char*)pt, &length,
                         (unsigned char*)ciphertext, strlen(ciphertext)) != 1)
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
