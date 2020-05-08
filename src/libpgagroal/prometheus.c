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
#include <prometheus.h>
#include <utils.h>

#define ZF_LOG_TAG "prometheus"
#include <zf_log.h>

/* system */
#include <ev.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define CHUNK_SIZE 32768

#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_METRICS 2

static int resolve_page(struct message* msg);
static int unknown_page(int client_fd);
static int home_page(int client_fd);
static int metrics_page(int client_fd, void* shmem, void* pipeline_shmem);

static void connection_information(int client_fd, void* shmem);
static void limit_information(int client_fd, void* shmem);

static int send_chunk(int client_fd, char* data);

static char* append(char* orig, char* s);
static char* append_int(char* orig, int i);

void
pgagroal_prometheus(int client_fd, void* shmem, void* pipeline_shmem)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgagroal_start_logging(shmem);
   pgagroal_memory_init(shmem);

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_prometheus: connect %d", client_fd);

   status = pgagroal_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      home_page(client_fd);
   }
   else if (page == PAGE_METRICS)
   {
      metrics_page(client_fd, shmem, pipeline_shmem);
   }
   else
   {
      unknown_page(client_fd);
   }

   ZF_LOGD("pgagroal_prometheus: disconnect %d", client_fd);
   pgagroal_disconnect(client_fd);

   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(0);

error:

   ZF_LOGD("pgagroal_prometheus: disconnect %d", client_fd);
   pgagroal_disconnect(client_fd);

   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(1);
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (strncmp((char*)msg->data, "GET", 3) != 0)
   {
      ZF_LOGD("Promethus: Not a GET request");
      return PAGE_UNKNOWN;
   }

   index = 4;
   from = (char*)msg->data + index;

   while (pgagroal_read_byte(msg->data + index) != ' ')
   {
      index++;
   }

   pgagroal_write_byte(msg->data + index, '\0');

   if (strcmp(from, "/") == 0 || strcmp(from, "/index.html") == 0)
   {
      return PAGE_HOME;
   }
   else if (strcmp(from, "/metrics") == 0)
   {
      return PAGE_METRICS;
   }

   return PAGE_UNKNOWN;
}

static int
unknown_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   
   data = append(data, "HTTP/1.1 403 Forbidden\r\n");
   data = append(data, "Date: ");
   data = append(data, &time_buf[0]);

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static int
home_page(int client_fd)
{
   char* data = NULL;
   char* body = NULL;
   char body_length[5];
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   memset(&body_length, 0, sizeof(body_length));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   
   data = append(data, "HTTP/1.1 200 OK\r\n");

   body = append(body, "<html>\n");
   body = append(body, "<head>\n");
   body = append(body, "  <title>pgagroal exporter</title>\n");
   body = append(body, "</head>\n");
   body = append(body, "<body>\n");
   body = append(body, "  <h1>pgagroal exporter</h1>\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <a href=\"/metrics\">Metrics</a>\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <h2>pgagroal_active_connections</h2>\n");
   body = append(body, "  The number of active connections\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <h2>pgagroal_total_connections</h2>\n");
   body = append(body, "  The number of total connections\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <h2>pgagroal_max_connections</h2>\n");
   body = append(body, "  The maximum number of connections\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <h2>pgagroal_connection</h2>\n");
   body = append(body, "  Connection information\n");
   body = append(body, "  <table border=\"1\">\n");
   body = append(body, "    <tbody>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>id</td>\n");
   body = append(body, "        <td>The connection identifier</td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>user</td>\n");
   body = append(body, "        <td>The user name</td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>database</td>\n");
   body = append(body, "        <td>The database</td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>state</td>\n");
   body = append(body, "        <td>The connection state\n");
   body = append(body, "          <ul>\n");
   body = append(body, "            <li>not_init</li>\n");
   body = append(body, "            <li>init</li>\n");
   body = append(body, "            <li>free</li>\n");
   body = append(body, "            <li>in_use</li>\n");
   body = append(body, "            <li>gracefully</li>\n");
   body = append(body, "            <li>flush</li>\n");
   body = append(body, "            <li>idle_check</li>\n");
   body = append(body, "            <li>validation</li>\n");
   body = append(body, "            <li>remove</li>\n");
   body = append(body, "          </ul>\n");
   body = append(body, "        </td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "    </tbody>\n");
   body = append(body, "  </table>\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <h2>pgagroal_limit</h2>\n");
   body = append(body, "  Limit information\n");
   body = append(body, "  <table border=\"1\">\n");
   body = append(body, "    <tbody>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>user</td>\n");
   body = append(body, "        <td>The user name</td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>database</td>\n");
   body = append(body, "        <td>The database</td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "      <tr>\n");
   body = append(body, "        <td>type</td>\n");
   body = append(body, "        <td>The information type\n");
   body = append(body, "          <ul>\n");
   body = append(body, "            <li>min</li>\n");
   body = append(body, "            <li>initial</li>\n");
   body = append(body, "            <li>max</li>\n");
   body = append(body, "            <li>active</li>\n");
   body = append(body, "          </ul>\n");
   body = append(body, "        </td>\n");
   body = append(body, "      </tr>\n");
   body = append(body, "    </tbody>\n");
   body = append(body, "  </table>\n");
   body = append(body, "  <p>\n");
   body = append(body, "  <a href=\"https://agroal.github.io/pgagroal/\">agroal.github.io/pgagroal/</a>\n");
   body = append(body, "</body>\n");
   body = append(body, "</html>\n");

   sprintf(&body_length[0], "%ld", strlen(body));

   data = append(data, "Content-Length: ");
   data = append(data, &body_length[0]);
   data = append(data, "\r\n");
   data = append(data, "Content-Type: text/html; charset=utf-8\r\n");
   data = append(data, "Date: ");
   data = append(data, &time_buf[0]);
   data = append(data, "\r\n");
   data = append(data, body);

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);

   free(body);
   free(data);

   return status;
}

static int
metrics_page(int client_fd, void* shmem, void* pipeline_shmem)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   
   data = append(data, "HTTP/1.1 200 OK\r\n");
   data = append(data, "Content-Type: text/plain; version=0.0.1; charset=utf-8\r\n");
   data = append(data, "Transfer-Encoding: chunked\r\n");
   data = append(data, "Date: ");
   data = append(data, &time_buf[0]);
   data = append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);
   data = NULL;

   connection_information(client_fd, shmem);
   limit_information(client_fd, shmem);

   /* Footer */
   data = append(data, "0\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);
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

static void
connection_information(int client_fd, void* shmem)
{
   char* data = NULL;
   int gauge;
   struct message msg;
   struct configuration* config;

   memset(&msg, 0, sizeof(struct message));

   config = (struct configuration*)shmem;

   data = append(data, "#HELP pgagroal_active_connections The number of active connections\n");
   data = append(data, "#TYPE pgagroal_active_connections gauge\n");
   data = append(data, "pgagroal_active_connections ");
   data = append_int(data, config->active_connections);
   data = append(data, "\n");

   gauge = 0;
   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);
      switch (state)
      {
         case STATE_IN_USE:
         case STATE_INIT:
         case STATE_FREE:
         case STATE_GRACEFULLY:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            gauge++;
            break;
         default:
            break;
      }
   }

   data = append(data, "#HELP pgagroal_total_connections The total number of connections\n");
   data = append(data, "#TYPE pgagroal_total_connections gauge\n");
   data = append(data, "pgagroal_total_connections ");
   data = append_int(data, gauge);
   data = append(data, "\n");

   data = append(data, "#HELP pgagroal_max_connections The maximum number of connections\n");
   data = append(data, "#TYPE pgagroal_max_connections gauge\n");
   data = append(data, "pgagroal_max_connections ");
   data = append_int(data, config->max_connections);
   data = append(data, "\n");

   data = append(data, "#HELP pgagroal_connection The connection information\n");
   data = append(data, "#TYPE pgagroal_connection gauge\n");
   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);

      data = append(data, "pgagroal_connection(");

      data = append(data, "id=\"");
      data = append_int(data, i);
      data = append(data, "\",");

      data = append(data, "user=\"");
      data = append(data, config->connections[i].username);
      data = append(data, "\",");

      data = append(data, "database=\"");
      data = append(data, config->connections[i].database);
      data = append(data, "\",");
      
      data = append(data, "state=\"");

      switch (state)
      {
         case STATE_NOTINIT:
            data = append(data, "not_init");
            break;
         case STATE_INIT:
            data = append(data, "init");
            break;
         case STATE_FREE:
            data = append(data, "free");
            break;
         case STATE_IN_USE:
            data = append(data, "in_use");
            break;
         case STATE_GRACEFULLY:
            data = append(data, "gracefully");
            break;
         case STATE_FLUSH:
            data = append(data, "flush");
            break;
         case STATE_IDLE_CHECK:
            data = append(data, "idle_check");
            break;
         case STATE_VALIDATION:
            data = append(data, "validation");
            break;
         case STATE_REMOVE:
            data = append(data, "remove");
            break;
         default:
            break;
      }

      data = append(data, "\") ");

      switch (state)
      {
         case STATE_NOTINIT:
            data = append(data, "0");
            break;
         case STATE_INIT:
         case STATE_FREE:
         case STATE_IN_USE:
         case STATE_GRACEFULLY:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            data = append(data, "1");
            break;
         default:
            break;
      }

      data = append(data, "\n");

      if (strlen(data) > CHUNK_SIZE)
      {
         send_chunk(client_fd, data);
         free(data);
         data = NULL;
      }
   }

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }
}

static void
limit_information(int client_fd, void* shmem)
{
   char* data = NULL;
   struct message msg;
   struct configuration* config;

   memset(&msg, 0, sizeof(struct message));

   config = (struct configuration*)shmem;

   if (config->number_of_limits > 0)
   {
      data = append(data, "#HELP pgagroal_limit The limit information\n");
      data = append(data, "#TYPE pgagroal_limit gauge\n");
      for (int i = 0; i < config->number_of_limits; i++)
      {
         data = append(data, "pgagroal_limit(");

         data = append(data, "user=\"");
         data = append(data, config->limits[i].username);
         data = append(data, "\",");

         data = append(data, "database=\"");
         data = append(data, config->limits[i].database);
         data = append(data, "\",");
      
         data = append(data, "type=\"min\") ");
         data = append_int(data, config->limits[i].min_size);
         data = append(data, "\n");
         
         data = append(data, "pgagroal_limit(");

         data = append(data, "user=\"");
         data = append(data, config->limits[i].username);
         data = append(data, "\",");

         data = append(data, "database=\"");
         data = append(data, config->limits[i].database);
         data = append(data, "\",");
      
         data = append(data, "type=\"initial\") ");
         data = append_int(data, config->limits[i].initial_size);
         data = append(data, "\n");
         
         data = append(data, "pgagroal_limit(");

         data = append(data, "user=\"");
         data = append(data, config->limits[i].username);
         data = append(data, "\",");

         data = append(data, "database=\"");
         data = append(data, config->limits[i].database);
         data = append(data, "\",");
      
         data = append(data, "type=\"max\") ");
         data = append_int(data, config->limits[i].max_size);
         data = append(data, "\n");
         
         data = append(data, "pgagroal_limit(");

         data = append(data, "user=\"");
         data = append(data, config->limits[i].username);
         data = append(data, "\",");

         data = append(data, "database=\"");
         data = append(data, config->limits[i].database);
         data = append(data, "\",");
      
         data = append(data, "type=\"active\") ");
         data = append_int(data, config->limits[i].active_connections);
         data = append(data, "\n");
         
         if (strlen(data) > CHUNK_SIZE)
         {
            send_chunk(client_fd, data);
            free(data);
            data = NULL;
         }
      }

      if (data != NULL)
      {
         send_chunk(client_fd, data);
         free(data);
         data = NULL;
      }
   }
}

static int
send_chunk(int client_fd, char* data)
{
   int status;
   char* m = NULL;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   m = malloc(7);
   memset(m, 0, 7);

   sprintf(m, "%lX\r\n", strlen(data));

   m = append(m, data);
   
   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgagroal_write_message(NULL, client_fd, &msg);

   free(m);

   return status;
}

static char*
append(char* orig, char* s)
{
   size_t orig_length;
   size_t s_length;
   char* n = NULL;

   if (s == NULL)
   {
      return orig;
   }

   if (orig != NULL)
   {
      orig_length = strlen(orig);
   }
   else
   {
      orig_length = 0;
   }

   s_length = strlen(s);

   n = (char*)realloc(orig, orig_length + s_length + 1);

   memcpy(n + orig_length, s, s_length); 

   n[orig_length + s_length] = '\0';

   return n;
}

static char*
append_int(char* orig, int i)
{
   char number[32];

   memset(&number[0], 0, sizeof(number));
   sprintf(&number[0], "%d", i);
   orig = append(orig, number);

   return orig;
}
