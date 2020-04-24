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
#include <memory.h>
#include <message.h>
#include <network.h>
#include <worker.h>
#include <utils.h>

#define ZF_LOG_TAG "message"
#include <zf_log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

static int read_message(int socket, bool block, int timeout, struct message** msg);
static int write_message(int socket, bool nodelay, struct message* msg);

static int ssl_read_message(SSL* ssl, bool block, int timeout, struct message** msg);
static int ssl_write_message(SSL* ssl, bool nodelay, struct message* msg);

int
pgagroal_read_block_message(SSL* ssl, int socket, struct message** msg)
{
   if (ssl == NULL)
   {
      return read_message(socket, true, 0, msg);
   }

   return ssl_read_message(ssl, true, 0, msg);
}

int
pgagroal_read_timeout_message(SSL* ssl, int socket, int timeout, struct message** msg)
{
   if (ssl == NULL)
   {
      return read_message(socket, true, timeout, msg);
   }

   return ssl_read_message(ssl, true, timeout, msg);
}

int
pgagroal_write_message(SSL* ssl, int socket, struct message* msg)
{
   if (ssl == NULL)
   {
      return write_message(socket, false, msg);
   }

   return ssl_write_message(ssl, false, msg);
}

int
pgagroal_write_nodelay_message(SSL* ssl, int socket, struct message* msg)
{
   if (ssl == NULL)
   {
      return write_message(socket, true, msg);
   }

   return ssl_write_message(ssl, true, msg);
}

int
pgagroal_read_socket_message(int socket, struct message** msg)
{
   return read_message(socket, false, 0, msg);
}

int
pgagroal_write_socket_message(int socket, struct message* msg)
{
   return write_message(socket, false, msg);
}

int
pgagroal_read_ssl_message(SSL* ssl, struct message** msg)
{
   return ssl_read_message(ssl, false, 0, msg);
}

int
pgagroal_write_ssl_message(SSL* ssl, struct message* msg)
{
   return ssl_write_message(ssl, false, msg);
}

int
pgagroal_create_message(void* data, ssize_t length, struct message** msg)
{
   struct message* copy = NULL;

   copy = (struct message*)malloc(sizeof(struct message));
   copy->data = malloc(length);

   copy->kind = pgagroal_read_byte(data);
   copy->length = length;
   memcpy(copy->data, data, length);
     
   *msg = copy;

   return MESSAGE_STATUS_OK;
}

void
pgagroal_free_message(struct message* msg)
{
   pgagroal_memory_free();
}

struct message*
pgagroal_copy_message(struct message* msg)
{
   struct message* copy = NULL;

   copy = (struct message*)malloc(sizeof(struct message));
   copy->data = malloc(msg->length);

   copy->kind = msg->kind;
   copy->length = msg->length;
   memcpy(copy->data, msg->data, msg->length);
     
   return copy;
}

void
pgagroal_free_copy_message(struct message* msg)
{
   if (msg)
   {
      if (msg->data)
      {
         free(msg->data);
         msg->data = NULL;
      }

      free(msg);
      msg = NULL;
   }
}

int
pgagroal_write_empty(SSL* ssl, int socket)
{
   char zero[1];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&zero, 0, sizeof(zero));

   msg.kind = 0;
   msg.length = 1;
   msg.data = &zero;

   if (ssl == NULL)
   {
      return write_message(socket, false, &msg);
   }

   return ssl_write_message(ssl, false, &msg);
}

int
pgagroal_write_notice(SSL* ssl, int socket)
{
   char notice[1];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&notice, 0, sizeof(notice));

   notice[0] = 'N';

   msg.kind = 'N';
   msg.length = 1;
   msg.data = &notice;

   if (ssl == NULL)
   {
      return write_message(socket, false, &msg);
   }

   return ssl_write_message(ssl, false, &msg);
}

int
pgagroal_write_tls(SSL* ssl, int socket)
{
   char tls[1];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&tls, 0, sizeof(tls));

   tls[0] = 'S';

   msg.kind = 'S';
   msg.length = 1;
   msg.data = &tls;

   if (ssl == NULL)
   {
      return write_message(socket, false, &msg);
   }

   return ssl_write_message(ssl, false, &msg);
}

int
pgagroal_write_pool_full(SSL* ssl, int socket)
{
   int size = 51;
   char pool_full[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&pool_full, 0, sizeof(pool_full));

   pgagroal_write_byte(&pool_full, 'E');
   pgagroal_write_int32(&(pool_full[1]), size - 1);
   pgagroal_write_string(&(pool_full[5]), "SFATAL");
   pgagroal_write_string(&(pool_full[12]), "VFATAL");
   pgagroal_write_string(&(pool_full[19]), "C53300");
   pgagroal_write_string(&(pool_full[26]), "Mconnection pool is full");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &pool_full;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_connection_refused(SSL* ssl, int socket)
{
   int size = 46;
   char connection_refused[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&connection_refused, 0, sizeof(connection_refused));

   pgagroal_write_byte(&connection_refused, 'E');
   pgagroal_write_int32(&(connection_refused[1]), size - 1);
   pgagroal_write_string(&(connection_refused[5]), "SFATAL");
   pgagroal_write_string(&(connection_refused[12]), "VFATAL");
   pgagroal_write_string(&(connection_refused[19]), "C53300");
   pgagroal_write_string(&(connection_refused[26]), "Mconnection refused");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &connection_refused;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_connection_refused_old(SSL* ssl, int socket)
{
   int size = 20;
   char connection_refused[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&connection_refused, 0, sizeof(connection_refused));

   pgagroal_write_byte(&connection_refused, 'E');
   pgagroal_write_string(&(connection_refused[1]), "connection refused");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &connection_refused;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_bad_password(SSL* ssl, int socket, char* username)
{
   int size = strlen(username);
   size += 84;

   char badpassword[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&badpassword, 0, sizeof(badpassword));

   pgagroal_write_byte(&badpassword, 'E');
   pgagroal_write_int32(&(badpassword[1]), size - 1);
   pgagroal_write_string(&(badpassword[5]), "SFATAL");
   pgagroal_write_string(&(badpassword[12]), "VFATAL");
   pgagroal_write_string(&(badpassword[19]), "C28P01");
   pgagroal_write_string(&(badpassword[26]), "Mpassword authentication failed for user \"");
   pgagroal_write_string(&(badpassword[68]), username);
   pgagroal_write_string(&(badpassword[68 + strlen(username)]), "\"");
   pgagroal_write_string(&(badpassword[68 + strlen(username) + 2]), "Rauth_failed");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &badpassword;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_unsupported_security_model(SSL* ssl, int socket, char* username)
{
   int size = strlen(username);
   size += 66;
   
   char unsupported[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&unsupported, 0, sizeof(unsupported));

   pgagroal_write_byte(&unsupported, 'E');
   pgagroal_write_int32(&(unsupported[1]), size - 1);
   pgagroal_write_string(&(unsupported[5]), "SFATAL");
   pgagroal_write_string(&(unsupported[12]), "VFATAL");
   pgagroal_write_string(&(unsupported[19]), "C28000");
   pgagroal_write_string(&(unsupported[26]), "Munsupported security model for user \"");
   pgagroal_write_string(&(unsupported[64]), username);
   pgagroal_write_string(&(unsupported[size - 2]), "\"");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &unsupported;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_no_hba_entry(SSL* ssl, int socket, char* username, char* database, char* address)
{
   int size = strlen(username);
   size += strlen(database);
   size += strlen(address);
   size += 88;

   char no_hba[size];
   struct message msg;
   int offset = 64;

   memset(&msg, 0, sizeof(struct message));
   memset(&no_hba, 0, sizeof(no_hba));

   pgagroal_write_byte(&no_hba, 'E');
   pgagroal_write_int32(&(no_hba[1]), size - 1);
   pgagroal_write_string(&(no_hba[5]), "SFATAL");
   pgagroal_write_string(&(no_hba[12]), "VFATAL");
   pgagroal_write_string(&(no_hba[19]), "C28000");
   pgagroal_write_string(&(no_hba[26]), "Mno pgagroal_hba.conf entry for host \"");
   pgagroal_write_string(&(no_hba[64]), address);

   offset += strlen(address);

   pgagroal_write_string(&(no_hba[offset]), "\", user \"");

   offset += 9;

   pgagroal_write_string(&(no_hba[offset]), username);

   offset += strlen(username);

   pgagroal_write_string(&(no_hba[offset]), "\", database \"");

   offset += 13;

   pgagroal_write_string(&(no_hba[offset]), database);

   offset += strlen(database);

   pgagroal_write_string(&(no_hba[offset]), "\"");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &no_hba;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_deallocate_all(SSL* ssl, int socket)
{
   int status;
   int size = 21;

   char deallocate[size];
   struct message msg;
   struct message* reply = NULL;

   memset(&msg, 0, sizeof(struct message));
   memset(&deallocate, 0, sizeof(deallocate));

   pgagroal_write_byte(&deallocate, 'Q');
   pgagroal_write_int32(&(deallocate[1]), size - 1);
   pgagroal_write_string(&(deallocate[5]), "DEALLOCATE ALL;");

   msg.kind = 'Q';
   msg.length = size;
   msg.data = &deallocate;

   if (ssl == NULL)
   {
      status = write_message(socket, true, &msg);
   }
   else
   {
      status = ssl_write_message(ssl, true, &msg);
   }
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (ssl == NULL)
   {
      status = read_message(socket, true, 0, &reply);
   }
   else
   {
      status = ssl_read_message(ssl, true, 0, &reply);
   }
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(reply);
   
   return 0;

error:
   if (reply)
      pgagroal_free_message(reply);

   return 1;
}

int
pgagroal_write_reset_all(SSL* ssl, int socket)
{
   int status;
   int size = 16;

   char reset[size];
   struct message msg;
   struct message* reply = NULL;

   memset(&msg, 0, sizeof(struct message));
   memset(&reset, 0, sizeof(reset));

   pgagroal_write_byte(&reset, 'Q');
   pgagroal_write_int32(&(reset[1]), size - 1);
   pgagroal_write_string(&(reset[5]), "RESET ALL;");

   msg.kind = 'Q';
   msg.length = size;
   msg.data = &reset;

   if (ssl == NULL)
   {
      status = write_message(socket, true, &msg);
   }
   else
   {
      status = ssl_write_message(ssl, true, &msg);
   }
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (ssl == NULL)
   {
      status = read_message(socket, true, 0, &reply);
   }
   else
   {
      status = ssl_read_message(ssl, true, 0, &reply);
   }
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgagroal_free_message(reply);

   return 0;

error:
   if (reply)
      pgagroal_free_message(reply);

   return 1;
}

int
pgagroal_write_terminate(SSL* ssl, int socket)
{
   char terminate[5];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&terminate, 0, sizeof(terminate));

   terminate[0] = 'X';
   terminate[4] = 4;

   msg.kind = 'X';
   msg.length = 5;
   msg.data = &terminate;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_write_auth_password(SSL* ssl, int socket)
{
   char password[9];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&password, 0, sizeof(password));

   password[0] = 'R';
   pgagroal_write_int32(&(password[1]), 8);
   pgagroal_write_int32(&(password[5]), 3);

   msg.kind = 'R';
   msg.length = 9;
   msg.data = &password;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_create_auth_password_response(char* password, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 6 + strlen(password);

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

   pgagroal_write_byte(m->data, 'p');
   pgagroal_write_int32(m->data + 1, size - 1);
   pgagroal_write_string(m->data + 5, password);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_write_auth_md5(SSL* ssl, int socket, char salt[4])
{
   char md5[13];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&md5, 0, sizeof(md5));

   md5[0] = 'R';
   pgagroal_write_int32(&(md5[1]), 12);
   pgagroal_write_int32(&(md5[5]), 5);
   pgagroal_write_byte(&(md5[9]), salt[0]);
   pgagroal_write_byte(&(md5[10]), salt[1]);
   pgagroal_write_byte(&(md5[11]), salt[2]);
   pgagroal_write_byte(&(md5[12]), salt[3]);

   msg.kind = 'R';
   msg.length = 13;
   msg.data = &md5;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_create_auth_md5_response(char* md5, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + strlen(md5) + 1;

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

   pgagroal_write_byte(m->data, 'p');
   pgagroal_write_int32(m->data + 1, size - 1);
   pgagroal_write_string(m->data + 5, md5);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_write_auth_scram256(SSL* ssl, int socket)
{
   char scram[24];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&scram, 0, sizeof(scram));

   scram[0] = 'R';
   pgagroal_write_int32(&(scram[1]), 23);
   pgagroal_write_int32(&(scram[5]), 10);
   pgagroal_write_string(&(scram[9]), "SCRAM-SHA-256");

   msg.kind = 'R';
   msg.length = 24;
   msg.data = &scram;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_create_auth_scram256_response(char* nounce, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 13 + 4 + 9 + strlen(nounce);

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

   pgagroal_write_byte(m->data, 'p');
   pgagroal_write_int32(m->data + 1, size - 1);
   pgagroal_write_string(m->data + 5, "SCRAM-SHA-256");
   pgagroal_write_string(m->data + 22, " n,,n=,r=");
   pgagroal_write_string(m->data + 31, nounce);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_create_auth_scram256_continue(char* cn, char* sn, char* salt, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 4 + 2 + strlen(cn) + strlen(sn) + 3 + strlen(salt) + 7;

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'R';
   m->length = size;

   pgagroal_write_byte(m->data, 'R');
   pgagroal_write_int32(m->data + 1, size - 1);
   pgagroal_write_int32(m->data + 5, 11);
   pgagroal_write_string(m->data + 9, "r=");
   pgagroal_write_string(m->data + 11, cn);
   pgagroal_write_string(m->data + 11 + strlen(cn), sn);
   pgagroal_write_string(m->data + 11 + strlen(cn) + strlen(sn), ",s=");
   pgagroal_write_string(m->data + 11 + strlen(cn) + strlen(sn) + 3, salt);
   pgagroal_write_string(m->data + 11 + strlen(cn) + strlen(sn) + 3 + strlen(salt), ",i=4096");

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_create_auth_scram256_continue_response(char* wp, char* p, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + strlen(wp) + 3 + strlen(p);

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

   pgagroal_write_byte(m->data, 'p');
   pgagroal_write_int32(m->data + 1, size - 1);
   pgagroal_write_string(m->data + 5, wp);
   pgagroal_write_string(m->data + 5 + strlen(wp), ",p=");
   pgagroal_write_string(m->data + 5 + strlen(wp) + 3, p);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_create_auth_scram256_final(char* ss, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 4 + 2 + strlen(ss);

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'R';
   m->length = size;

   pgagroal_write_byte(m->data, 'R');
   pgagroal_write_int32(m->data + 1, size - 1);
   pgagroal_write_int32(m->data + 5, 12);
   pgagroal_write_string(m->data + 9, "v=");
   pgagroal_write_string(m->data + 11, ss);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_write_auth_success(SSL* ssl, int socket)
{
   char success[9];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&success, 0, sizeof(success));

   success[0] = 'R';
   pgagroal_write_int32(&(success[1]), 8);
   pgagroal_write_int32(&(success[5]), 0);

   msg.kind = 'R';
   msg.length = 9;
   msg.data = &success;

   if (ssl == NULL)
   {
      return write_message(socket, true, &msg);
   }

   return ssl_write_message(ssl, true, &msg);
}

int
pgagroal_create_startup_message(char* username, char* database, struct message** msg)
{
   struct message* m = NULL;
   size_t size;
   size_t us;
   size_t ds;

   us = strlen(username);
   ds = strlen(database);
   size = 4 + 4 + 4 + 1 + us + 1 + 8 + 1 + ds + 1 + 17 + 9 + 1;

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 0;
   m->length = size;

   pgagroal_write_int32(m->data, size);
   pgagroal_write_int32(m->data + 4, 196608);
   pgagroal_write_string(m->data + 8, "user");
   pgagroal_write_string(m->data + 13, username);
   pgagroal_write_string(m->data + 13 + us + 1, "database");
   pgagroal_write_string(m->data + 13 + us + 1 + 9, database);
   pgagroal_write_string(m->data + 13 + us + 1 + 9 + ds + 1, "application_name");
   pgagroal_write_string(m->data + 13 + us + 1 + 9 + ds + 1 + 17, "pgagroal");

   *msg = m;

   return MESSAGE_STATUS_OK;
}

bool
pgagroal_connection_isvalid(int socket)
{
   int status;
   int size = 15;

   char valid[size];
   struct message msg;
   struct message* reply = NULL;

   memset(&msg, 0, sizeof(struct message));
   memset(&valid, 0, sizeof(valid));

   pgagroal_write_byte(&valid, 'Q');
   pgagroal_write_int32(&(valid[1]), size - 1);
   pgagroal_write_string(&(valid[5]), "SELECT 1;");

   msg.kind = 'Q';
   msg.length = size;
   msg.data = &valid;

   status = write_message(socket, true, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = read_message(socket, true, 0, &reply);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (reply->kind == 'E')
   {
      goto error;
   }

   pgagroal_free_message(reply);

   return true;

error:
   if (reply)
      pgagroal_free_message(reply);

   return false;
}

void
pgagroal_log_message(struct message* msg)
{
   if (msg == NULL)
   {
      ZF_LOGI("Message is NULL");
   }
   else if (msg->data == NULL)
   {
      ZF_LOGI("Message DATA is NULL");
   }
   else
   {
      ZF_LOGI("Size: %zd", msg->length);
      ZF_LOGI_MEM(msg->data, msg->length,
                  "Message %p:", (const void *)msg->data);
   }
}

static int
read_message(int socket, bool block, int timeout, struct message** msg)
{
   bool keep_read = false;
   ssize_t numbytes;  
   struct timeval tv;
   struct message* m = NULL;

   if (unlikely(timeout > 0))
   {
      tv.tv_sec = timeout;
      tv.tv_usec = 0;
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

      pgagroal_memory_free();
   }

   do
   {
      m = pgagroal_memory_message();

      numbytes = read(socket, m->data, m->max_length);

      if (likely(numbytes > 0))
      {
         m->kind = (signed char)(*((char*)m->data));
         m->length = numbytes;
         *msg = m;

         if (unlikely(timeout > 0))
         {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
         }

         return MESSAGE_STATUS_OK;
      }
      else if (numbytes == 0)
      {
         pgagroal_memory_free();

         if ((errno == EAGAIN || errno == EWOULDBLOCK) && block)
         {
            keep_read = true;
            errno = 0;
         }
         else
         {
            if (unlikely(timeout > 0))
            {
               tv.tv_sec = 0;
               tv.tv_usec = 0;
               setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
            }

            return MESSAGE_STATUS_ZERO;
         }
      }
      else
      {
         pgagroal_memory_free();

         if ((errno == EAGAIN || errno == EWOULDBLOCK) && block)
         {
            keep_read = true;
            errno = 0;
         }
         else
         {
            keep_read = false;
         }
      }
   } while (keep_read);

   if (unlikely(timeout > 0))
   {
      tv.tv_sec = 0;
      tv.tv_usec = 0;
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

      pgagroal_memory_free();
   }

   return MESSAGE_STATUS_ERROR;
}

static int
write_message(int socket, bool nodelay, struct message* msg)
{
   bool keep_write = false;
   ssize_t numbytes;  

#ifdef DEBUG
   assert(msg != NULL);
#endif

   do
   {
      numbytes = write(socket, msg->data, msg->length);

      if (likely(numbytes == msg->length))
      {
         return MESSAGE_STATUS_OK;
      }
      else if (numbytes != -1)
      {
         ZF_LOGD("Write - %zd vs %zd", numbytes, msg->length);
         keep_write = true;
      }
      else
      {
         if (!nodelay)
         {
            return MESSAGE_STATUS_ERROR;
         }
         else
         {
            keep_write = true;
         }
      }
   } while (keep_write);

   return MESSAGE_STATUS_ERROR;
}

static int
ssl_read_message(SSL* ssl, bool block, int timeout, struct message** msg)
{
   bool keep_read = false;
   ssize_t numbytes;
   time_t start_time;
   struct message* m = NULL;

   if (unlikely(timeout > 0))
   {
      start_time = time(NULL);
   }

   do
   {
      m = pgagroal_memory_message();

      numbytes = SSL_read(ssl, m->data, m->max_length);

      if (likely(numbytes > 0))
      {
         m->kind = (signed char)(*((char*)m->data));
         m->length = numbytes;
         *msg = m;

         return MESSAGE_STATUS_OK;
      }
      else
      {
         int err;

         pgagroal_memory_free();

         err = SSL_get_error(ssl, numbytes);
         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
               if (timeout > 0)
               {
                  struct timespec ts;

                  if (difftime(time(NULL), start_time) >= timeout)
                  {
                     return MESSAGE_STATUS_ZERO;
                  }

                  /* Sleep for 100ms */
                  ts.tv_sec = 0;
                  ts.tv_nsec = 100000000L;
                  nanosleep(&ts, NULL);
               }
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
               keep_read = true;
               break;
            case SSL_ERROR_SYSCALL:
               ZF_LOGE("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_read = false;
               break;
            case SSL_ERROR_SSL:
               ZF_LOGE("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               keep_read = false;
               break;
         }
         ERR_clear_error();
      }
   } while (keep_read);

   return MESSAGE_STATUS_ERROR;
}

static int
ssl_write_message(SSL* ssl, bool nodelay, struct message* msg)
{
   bool keep_write = false;
   ssize_t numbytes;

   do
   {
      numbytes = SSL_write(ssl, msg->data, msg->length);

      if (likely(numbytes == msg->length))
      {
         return MESSAGE_STATUS_OK;
      }
      else
      {
         int err;

         pgagroal_memory_free();

         err = SSL_get_error(ssl, numbytes);
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
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               ZF_LOGE("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               ZF_LOGE("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!nodelay || !keep_write)
         {
            return MESSAGE_STATUS_ERROR;
         }
      }
   } while (keep_write);

   return MESSAGE_STATUS_ERROR;
}
