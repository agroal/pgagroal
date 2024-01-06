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
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <utils.h>
#include <shmem.h>

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
#define BAD_REQUEST  3

#define FIVE_SECONDS           5
#define TEN_SECONDS           10
#define TWENTY_SECONDS        20
#define THIRTY_SECONDS        30
#define FOURTYFIVE_SECONDS    45
#define ONE_MINUTE            60
#define FIVE_MINUTES         300
#define TEN_MINUTES          600
#define TWENTY_MINUTES      1200
#define THIRTY_MINUTES      1800
#define FOURTYFIVE_MINUTES  2700
#define ONE_HOUR            3600
#define TWO_HOURS           7200
#define FOUR_HOURS         14400
#define SIX_HOURS          21600
#define TWELVE_HOURS       43200
#define TWENTYFOUR_HOURS   86400

static int resolve_page(struct message* msg);
static int unknown_page(int client_fd);
static int home_page(int client_fd);
static int metrics_page(int client_fd);
static int bad_request(int client_fd);

static void general_information(int client_fd);
static void connection_information(int client_fd);
static void limit_information(int client_fd);
static void session_information(int client_fd);
static void pool_information(int client_fd);
static void auth_information(int client_fd);
static void client_information(int client_fd);
static void internal_information(int client_fd);
static void connection_awaiting_information(int client_fd);

static int send_chunk(int client_fd, char* data);

static bool is_metrics_cache_configured(void);
static bool is_metrics_cache_valid(void);
static bool metrics_cache_append(char* data);
static bool metrics_cache_finalize(void);
static size_t metrics_cache_size_to_alloc(void);
static void metrics_cache_invalidate(void);

void
pgagroal_prometheus(int client_fd)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct configuration*)shmem;

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
      metrics_page(client_fd);
   }
   else if (page == PAGE_UNKNOWN)
   {
      unknown_page(client_fd);
   }
   else
   {
      bad_request(client_fd);
   }

   pgagroal_disconnect(client_fd);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);

error:

   pgagroal_log_debug("pgagroal_prometheus: disconnect %d", client_fd);
   pgagroal_disconnect(client_fd);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(1);
}

int
pgagroal_init_prometheus(size_t* p_size, void** p_shmem)
{
   size_t tmp_p_size = 0;
   void* tmp_p_shmem = NULL;
   struct configuration* config;
   struct prometheus* prometheus;

   config = (struct configuration*) shmem;

   *p_size = 0;
   *p_shmem = NULL;

   tmp_p_size = sizeof(struct prometheus) + (config->max_connections * sizeof(struct prometheus_connection));
   if (pgagroal_create_shared_memory(tmp_p_size, config->hugepage, &tmp_p_shmem))
   {
      goto error;
   }

   prometheus = (struct prometheus*)tmp_p_shmem;

   for (int i = 0; i < HISTOGRAM_BUCKETS; i++)
   {
      atomic_init(&prometheus->session_time[i], 0);
   }
   atomic_init(&prometheus->session_time_sum, 0);

   atomic_init(&prometheus->connection_error, 0);
   atomic_init(&prometheus->connection_kill, 0);
   atomic_init(&prometheus->connection_remove, 0);
   atomic_init(&prometheus->connection_timeout, 0);
   atomic_init(&prometheus->connection_return, 0);
   atomic_init(&prometheus->connection_invalid, 0);
   atomic_init(&prometheus->connection_get, 0);
   atomic_init(&prometheus->connection_idletimeout, 0);
   atomic_init(&prometheus->connection_max_connection_age, 0);
   atomic_init(&prometheus->connection_flush, 0);
   atomic_init(&prometheus->connection_success, 0);

   // awating connections are those on hold due to
   // the `blocking_timeout` setting
   atomic_init(&prometheus->connections_awaiting_total, 0);

   for (int i = 0; i < NUMBER_OF_LIMITS; i++)
   {
      atomic_init(&prometheus->connections_awaiting[i], 0);
   }

   atomic_init(&prometheus->auth_user_success, 0);
   atomic_init(&prometheus->auth_user_bad_password, 0);
   atomic_init(&prometheus->auth_user_error, 0);

   atomic_init(&prometheus->client_wait, 0);
   atomic_init(&prometheus->client_active, 0);
   atomic_init(&prometheus->client_wait_time, 0);

   atomic_init(&prometheus->query_count, 0);
   atomic_init(&prometheus->tx_count, 0);

   atomic_init(&prometheus->network_sent, 0);
   atomic_init(&prometheus->network_received, 0);

   atomic_init(&prometheus->client_sockets, 0);
   atomic_init(&prometheus->self_sockets, 0);

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      atomic_init(&prometheus->server_error[i], 0);
   }
   atomic_init(&prometheus->failed_servers, 0);

   for (int i = 0; i < config->max_connections; i++)
   {
      memset(&prometheus->prometheus_connections[i], 0, sizeof(struct prometheus_connection));
      atomic_init(&prometheus->prometheus_connections[i].query_count, 0);
   }

   *p_size = tmp_p_size;
   *p_shmem = tmp_p_shmem;

   return 0;

error:

   return 1;
}

void
pgagroal_prometheus_session_time(double time)
{
   unsigned long t;
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   t = (unsigned long)time;

   atomic_fetch_add(&prometheus->session_time_sum, t);

   if (t <= FIVE_SECONDS)
   {
      atomic_fetch_add(&prometheus->session_time[0], 1);
   }
   else if (t > FIVE_SECONDS && t <= TEN_SECONDS)
   {
      atomic_fetch_add(&prometheus->session_time[1], 1);
   }
   else if (t > TEN_SECONDS && t <= TWENTY_SECONDS)
   {
      atomic_fetch_add(&prometheus->session_time[2], 1);
   }
   else if (t > TWENTY_SECONDS && t <= THIRTY_SECONDS)
   {
      atomic_fetch_add(&prometheus->session_time[3], 1);
   }
   else if (t > THIRTY_SECONDS && t <= FOURTYFIVE_SECONDS)
   {
      atomic_fetch_add(&prometheus->session_time[4], 1);
   }
   else if (t > FOURTYFIVE_SECONDS && t <= ONE_MINUTE)
   {
      atomic_fetch_add(&prometheus->session_time[5], 1);
   }
   else if (t > ONE_MINUTE && t <= FIVE_MINUTES)
   {
      atomic_fetch_add(&prometheus->session_time[6], 1);
   }
   else if (t > FIVE_MINUTES && t <= TEN_MINUTES)
   {
      atomic_fetch_add(&prometheus->session_time[7], 1);
   }
   else if (t > TEN_MINUTES && t <= TWENTY_MINUTES)
   {
      atomic_fetch_add(&prometheus->session_time[8], 1);
   }
   else if (t > TWENTY_MINUTES && t <= THIRTY_MINUTES)
   {
      atomic_fetch_add(&prometheus->session_time[9], 1);
   }
   else if (t > THIRTY_MINUTES && t <= FOURTYFIVE_MINUTES)
   {
      atomic_fetch_add(&prometheus->session_time[10], 1);
   }
   else if (t > FOURTYFIVE_MINUTES && t <= ONE_HOUR)
   {
      atomic_fetch_add(&prometheus->session_time[11], 1);
   }
   else if (t > ONE_HOUR && t <= TWO_HOURS)
   {
      atomic_fetch_add(&prometheus->session_time[12], 1);
   }
   else if (t > TWO_HOURS && t <= FOUR_HOURS)
   {
      atomic_fetch_add(&prometheus->session_time[13], 1);
   }
   else if (t > FOUR_HOURS && t <= SIX_HOURS)
   {
      atomic_fetch_add(&prometheus->session_time[14], 1);
   }
   else if (t > SIX_HOURS && t <= TWELVE_HOURS)
   {
      atomic_fetch_add(&prometheus->session_time[15], 1);
   }
   else if (t > TWELVE_HOURS && t <= TWENTYFOUR_HOURS)
   {
      atomic_fetch_add(&prometheus->session_time[16], 1);
   }
   else
   {
      atomic_fetch_add(&prometheus->session_time[17], 1);
   }
}

void
pgagroal_prometheus_connection_error(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_error, 1);
}

void
pgagroal_prometheus_connection_kill(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_kill, 1);
}

void
pgagroal_prometheus_connection_remove(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_remove, 1);
}

void
pgagroal_prometheus_connection_timeout(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_timeout, 1);
}

void
pgagroal_prometheus_connection_return(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_return, 1);
}

void
pgagroal_prometheus_connection_invalid(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_invalid, 1);
}

void
pgagroal_prometheus_connection_get(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_get, 1);
}

void
pgagroal_prometheus_connection_idletimeout(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_idletimeout, 1);
}

void
pgagroal_prometheus_connection_max_connection_age(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_max_connection_age, 1);
}

void
pgagroal_prometheus_connection_awaiting(int limit_index)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   if (limit_index >= 0)
   {
      atomic_fetch_add(&prometheus->connections_awaiting[limit_index], 1);
   }

   atomic_fetch_add(&prometheus->connections_awaiting_total, 1);
}

void
pgagroal_prometheus_connection_unawaiting(int limit_index)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   if (limit_index >= 0 && atomic_load(&prometheus->connections_awaiting[limit_index]) > 0)
   {
      atomic_fetch_sub(&prometheus->connections_awaiting[limit_index], 1);
   }

   if (atomic_load(&prometheus->connections_awaiting_total) > 0)
   {
      atomic_fetch_sub(&prometheus->connections_awaiting_total, 1);
   }
}

void
pgagroal_prometheus_connection_flush(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_flush, 1);
}

void
pgagroal_prometheus_connection_success(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->connection_success, 1);
}

void
pgagroal_prometheus_auth_user_success(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->auth_user_success, 1);
}

void
pgagroal_prometheus_auth_user_bad_password(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->auth_user_bad_password, 1);
}

void
pgagroal_prometheus_auth_user_error(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->auth_user_error, 1);
}

void
pgagroal_prometheus_client_wait_add(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->client_wait, 1);
}

void
pgagroal_prometheus_client_wait_sub(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_sub(&prometheus->client_wait, 1);
}

void
pgagroal_prometheus_client_active_add(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->client_active, 1);
}

void
pgagroal_prometheus_client_active_sub(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_sub(&prometheus->client_active, 1);
}

void
pgagroal_prometheus_query_count_add(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->query_count, 1);
}

void
pgagroal_prometheus_query_count_specified_add(int slot)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->prometheus_connections[slot].query_count, 1);
}

void
pgagroal_prometheus_query_count_specified_reset(int slot)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_store(&prometheus->prometheus_connections[slot].query_count, 0);
}

void
pgagroal_prometheus_tx_count_add(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->tx_count, 1);
}

void
pgagroal_prometheus_network_sent_add(ssize_t s)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->network_sent, s);
}

void
pgagroal_prometheus_network_received_add(ssize_t s)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->network_received, s);
}

void
pgagroal_prometheus_client_sockets_add(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->client_sockets, 1);
}

void
pgagroal_prometheus_client_sockets_sub(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_sub(&prometheus->client_sockets, 1);
}

void
pgagroal_prometheus_self_sockets_add(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->self_sockets, 1);
}

void
pgagroal_prometheus_self_sockets_sub(void)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_sub(&prometheus->self_sockets, 1);
}

void
pgagroal_prometheus_reset(void)
{
   signed char cache_is_free;
   struct configuration* config;
   struct prometheus* prometheus;
   struct prometheus_cache* cache;

   config = (struct configuration*) shmem;
   prometheus = (struct prometheus*)prometheus_shmem;
   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   for (int i = 0; i < HISTOGRAM_BUCKETS; i++)
   {
      atomic_store(&prometheus->session_time[i], 0);
   }
   atomic_store(&prometheus->session_time_sum, 0);

   atomic_store(&prometheus->connection_error, 0);
   atomic_store(&prometheus->connection_kill, 0);
   atomic_store(&prometheus->connection_remove, 0);
   atomic_store(&prometheus->connection_timeout, 0);
   atomic_store(&prometheus->connection_return, 0);
   atomic_store(&prometheus->connection_invalid, 0);
   atomic_store(&prometheus->connection_get, 0);
   atomic_store(&prometheus->connection_idletimeout, 0);
   atomic_store(&prometheus->connection_max_connection_age, 0);
   atomic_store(&prometheus->connection_flush, 0);
   atomic_store(&prometheus->connection_success, 0);

   // awaiting connections are on hold due to `blocking_timeout`
   atomic_store(&prometheus->connections_awaiting_total, 0);
   for (int i = 0; i < NUMBER_OF_LIMITS; i++)
   {
      atomic_store(&prometheus->connections_awaiting[i], 0);
   }

   atomic_store(&prometheus->auth_user_success, 0);
   atomic_store(&prometheus->auth_user_bad_password, 0);
   atomic_store(&prometheus->auth_user_error, 0);

   atomic_store(&prometheus->client_active, 0);
   atomic_store(&prometheus->client_wait, 0);
   atomic_store(&prometheus->client_wait_time, 0);

   atomic_store(&prometheus->query_count, 0);
   atomic_store(&prometheus->tx_count, 0);

   atomic_store(&prometheus->network_sent, 0);
   atomic_store(&prometheus->network_received, 0);

   atomic_store(&prometheus->client_sockets, 0);
   atomic_store(&prometheus->self_sockets, 0);

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      atomic_store(&prometheus->server_error[i], 0);
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      atomic_store(&prometheus->prometheus_connections[i].query_count, 0);
   }

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      metrics_cache_invalidate();

      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      /* Sleep for 1ms */
      SLEEP_AND_GOTO(1000000L, retry_cache_locking);
   }
}

void
pgagroal_prometheus_server_error(int server)
{
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   atomic_fetch_add(&prometheus->server_error[server], 1);
}

void
pgagroal_prometheus_failed_servers(void)
{
   int count;
   struct prometheus* prometheus;
   struct configuration* config;

   prometheus = (struct prometheus*)prometheus_shmem;
   config = (struct configuration*) shmem;

   count = 0;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      signed char state = atomic_load(&config->servers[i].state);
      if (state == SERVER_FAILED)
      {
         count++;
      }
   }

   atomic_store(&prometheus->failed_servers, count);
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (msg->length < 3 || strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgagroal_log_debug("Promethus: Not a GET request");
      return BAD_REQUEST;
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
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgagroal_append(data, "HTTP/1.1 403 Forbidden\r\n");
   data = pgagroal_append(data, "Date: ");
   data = pgagroal_append(data, &time_buf[0]);
   data = pgagroal_append(data, "\r\n");

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
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgagroal_append(data, "HTTP/1.1 200 OK\r\n");
   data = pgagroal_append(data, "Content-Type: text/html; charset=utf-8\r\n");
   data = pgagroal_append(data, "Date: ");
   data = pgagroal_append(data, &time_buf[0]);
   data = pgagroal_append(data, "\r\n");
   data = pgagroal_append(data, "Transfer-Encoding: chunked\r\n");
   data = pgagroal_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto done;
   }

   free(data);
   data = NULL;

   data = pgagroal_append(data, "<!DOCTYPE html>\n");
   data = pgagroal_append(data, "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\">\n");
   data = pgagroal_append(data, "<head>\n");
   data = pgagroal_append(data, "  <title>pgagroal exporter</title>\n");
   data = pgagroal_append(data, "  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>");
   data = pgagroal_append(data, "  <style>\n");
   data = pgagroal_append(data, "   table { \n");
   data = pgagroal_append(data, "           margin: auto;\n");
   data = pgagroal_append(data, "           border: 2px solid black;\n");
   data = pgagroal_append(data, "         }\n");
   data = pgagroal_append(data, "   td { \n");
   data = pgagroal_append(data, "           border: 1px solid black;\n");
   data = pgagroal_append(data, "           text-align: center;;\n");
   data = pgagroal_append(data, "      }\n");
   data = pgagroal_append(data, "   ul { \n");
   data = pgagroal_append(data, "           text-align: left;\n");
   data = pgagroal_append(data, "      }\n");
   data = pgagroal_append(data, "   ol { \n");
   data = pgagroal_append(data, "           text-align: left;\n");
   data = pgagroal_append(data, "      }\n");
   data = pgagroal_append(data, "  </style>\n");
   data = pgagroal_append(data, "</head>\n");
   data = pgagroal_append(data, "<body>\n");
   data = pgagroal_append(data, "  <h1>pgagroal exporter</h1>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   <a href=\"/metrics\">Metrics</a>\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_state</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The state of pgagroal\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <table>\n");
   data = pgagroal_append(data, "    <tbody>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>value</td>\n");
   data = pgagroal_append(data, "        <td>State\n");
   data = pgagroal_append(data, "          <ol>\n");
   data = pgagroal_append(data, "            <li>Running</li>\n");
   data = pgagroal_append(data, "            <li>Graceful shutdown</li>\n");
   data = pgagroal_append(data, "          </ol>\n");
   data = pgagroal_append(data, "        </td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "    </tbody>\n");
   data = pgagroal_append(data, "  </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_pipeline_mode</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The mode of pipeline\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <table>\n");
   data = pgagroal_append(data, "    <tbody>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>value</td>\n");
   data = pgagroal_append(data, "        <td>Mode\n");
   data = pgagroal_append(data, "          <ol>\n");
   data = pgagroal_append(data, "            <li>Performance</li>\n");
   data = pgagroal_append(data, "            <li>Session</li>\n");
   data = pgagroal_append(data, "            <li>Transaction</li>\n");
   data = pgagroal_append(data, "          </ol>\n");
   data = pgagroal_append(data, "        </td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "    </tbody>\n");
   data = pgagroal_append(data, "  </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_server_error</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Errors for servers\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <table>\n");
   data = pgagroal_append(data, "    <tbody>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>name</td>\n");
   data = pgagroal_append(data, "        <td>The name of the server</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>state</td>\n");
   data = pgagroal_append(data, "        <td>The server state\n");
   data = pgagroal_append(data, "          <ul>\n");
   data = pgagroal_append(data, "            <li>not_init</li>\n");
   data = pgagroal_append(data, "            <li>primary</li>\n");
   data = pgagroal_append(data, "            <li>replica</li>\n");
   data = pgagroal_append(data, "            <li>failover</li>\n");
   data = pgagroal_append(data, "            <li>failed</li>\n");
   data = pgagroal_append(data, "          </ul>\n");
   data = pgagroal_append(data, "        </td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "    </tbody>\n");
   data = pgagroal_append(data, "  </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_failed_servers</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The number of failed servers. Only set if failover is enabled\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_wait_time</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The waiting time of clients\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_query_count</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The number of queries. Only session and transaction modes are supported\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_query_count</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The number of queries per connection. Only session and transaction modes are supported\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <table>\n");
   data = pgagroal_append(data, "    <tbody>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>id</td>\n");
   data = pgagroal_append(data, "        <td>The connection identifier</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>user</td>\n");
   data = pgagroal_append(data, "        <td>The user name</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>database</td>\n");
   data = pgagroal_append(data, "        <td>The database</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>application_name</td>\n");
   data = pgagroal_append(data, "        <td>The application name</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "    </tbody>\n");
   data = pgagroal_append(data, "  </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_tx_count</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The number of transactions. Only session and transaction modes are supported\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_active_connections</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The number of active connections\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_total_connections</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The number of total connections\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_max_connections</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   The maximum number of connections\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Connection information\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <table>\n");
   data = pgagroal_append(data, "    <tbody>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>id</td>\n");
   data = pgagroal_append(data, "        <td>The connection identifier</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>user</td>\n");
   data = pgagroal_append(data, "        <td>The user name</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>database</td>\n");
   data = pgagroal_append(data, "        <td>The database</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>application_name</td>\n");
   data = pgagroal_append(data, "        <td>The application name</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>state</td>\n");
   data = pgagroal_append(data, "        <td>The connection state\n");
   data = pgagroal_append(data, "          <ul>\n");
   data = pgagroal_append(data, "            <li>not_init</li>\n");
   data = pgagroal_append(data, "            <li>init</li>\n");
   data = pgagroal_append(data, "            <li>free</li>\n");
   data = pgagroal_append(data, "            <li>in_use</li>\n");
   data = pgagroal_append(data, "            <li>gracefully</li>\n");
   data = pgagroal_append(data, "            <li>flush</li>\n");
   data = pgagroal_append(data, "            <li>idle_check</li>\n");
   data = pgagroal_append(data, "            <li>max_connection_age</li>\n");
   data = pgagroal_append(data, "            <li>validation</li>\n");
   data = pgagroal_append(data, "            <li>remove</li>\n");
   data = pgagroal_append(data, "          </ul>\n");
   data = pgagroal_append(data, "        </td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "    </tbody>\n");
   data = pgagroal_append(data, "  </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_limit</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Limit information\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <table>\n");
   data = pgagroal_append(data, "    <tbody>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>user</td>\n");
   data = pgagroal_append(data, "        <td>The user name</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>database</td>\n");
   data = pgagroal_append(data, "        <td>The database</td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "      <tr>\n");
   data = pgagroal_append(data, "        <td>type</td>\n");
   data = pgagroal_append(data, "        <td>The information type\n");
   data = pgagroal_append(data, "          <ul>\n");
   data = pgagroal_append(data, "            <li>min</li>\n");
   data = pgagroal_append(data, "            <li>initial</li>\n");
   data = pgagroal_append(data, "            <li>max</li>\n");
   data = pgagroal_append(data, "            <li>active</li>\n");
   data = pgagroal_append(data, "          </ul>\n");
   data = pgagroal_append(data, "        </td>\n");
   data = pgagroal_append(data, "      </tr>\n");
   data = pgagroal_append(data, "    </tbody>\n");
   data = pgagroal_append(data, "  </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_limit_awaiting</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Connections awaiting on hold reported by limit entries\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "   <table>\n");
   data = pgagroal_append(data, "     <tbody>\n");
   data = pgagroal_append(data, "       <tr>\n");
   data = pgagroal_append(data, "         <td>user</td>\n");
   data = pgagroal_append(data, "         <td>The user name</td>\n");
   data = pgagroal_append(data, "       </tr>\n");
   data = pgagroal_append(data, "       <tr>\n");
   data = pgagroal_append(data, "         <td>database</td>\n");
   data = pgagroal_append(data, "         <td>The database</td>\n");
   data = pgagroal_append(data, "       </tr>\n");
   data = pgagroal_append(data, "     </tbody>\n");
   data = pgagroal_append(data, "   </table>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_session_time</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Histogram of session times\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_error</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection errors\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_kill</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection kills\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_remove</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection removes\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_timeout</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection time outs\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_return</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection returns\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_invalid</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection invalids\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_get</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection gets\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_idletimeout</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection idle timeouts\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_max_connection_age</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection max age timeouts\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_flush</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection flushes\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_success</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection successes\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_connection_awaiting</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of connection suspended due to <i>blocking_timeout</i>\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_auth_user_success</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of successful user authentications\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_auth_user_bad_password</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of bad passwords during user authentication\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_auth_user_error</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of errors during user authentication\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_client_wait</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of waiting clients\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_client_active</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of active clients\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_network_sent</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Bytes sent by clients. Only session and transaction modes are supported\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_network_received</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Bytes received from servers. Only session and transaction modes are supported\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_client_sockets</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of sockets the client used\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <h2>pgagroal_self_sockets</h2>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   Number of sockets used by pgagroal itself\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "  <p>\n");
   data = pgagroal_append(data, "   <a href=\"https://agroal.github.io/pgagroal/\">agroal.github.io/pgagroal/</a>\n");
   data = pgagroal_append(data, "  </p>\n");
   data = pgagroal_append(data, "</body>\n");
   data = pgagroal_append(data, "</html>\n");

   send_chunk(client_fd, data);
   free(data);
   data = NULL;

   /* Footer */
   data = pgagroal_append(data, "0\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);

done:
   if (data != NULL)
   {
      free(data);
   }

   return status;
}

static int
metrics_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;
   struct prometheus_cache* cache;
   signed char cache_is_free;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   memset(&msg, 0, sizeof(struct message));

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      // can serve the message out of cache?
      if (is_metrics_cache_configured() && is_metrics_cache_valid())
      {
         // serve the message directly out of the cache
         pgagroal_log_debug("Serving metrics out of cache (%d/%d bytes valid until %lld)",
                            strlen(cache->data),
                            cache->size,
                            cache->valid_until);

         msg.kind = 0;
         msg.length = strlen(cache->data);
         msg.data = cache->data;
      }
      else
      {
         // build the message without the cache
         metrics_cache_invalidate();

         now = time(NULL);

         memset(&time_buf, 0, sizeof(time_buf));
         ctime_r(&now, &time_buf[0]);
         time_buf[strlen(time_buf) - 1] = 0;

         data = pgagroal_append(data, "HTTP/1.1 200 OK\r\n");
         data = pgagroal_append(data, "Content-Type: text/plain; version=0.0.3; charset=utf-8\r\n");
         data = pgagroal_append(data, "Date: ");
         data = pgagroal_append(data, &time_buf[0]);
         data = pgagroal_append(data, "\r\n");
         metrics_cache_append(data);  // cache here to avoid the chunking for the cache
         data = pgagroal_append(data, "Transfer-Encoding: chunked\r\n");
         data = pgagroal_append(data, "\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgagroal_write_message(NULL, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            metrics_cache_invalidate();
            atomic_store(&cache->lock, STATE_FREE);

            goto error;
         }

         free(data);
         data = NULL;

         general_information(client_fd);
         connection_information(client_fd);
         limit_information(client_fd);
         session_information(client_fd);
         pool_information(client_fd);
         auth_information(client_fd);
         client_information(client_fd);
         internal_information(client_fd);
         connection_awaiting_information(client_fd);

         /* Footer */
         data = pgagroal_append(data, "0\r\n\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         metrics_cache_finalize();

      }

      // free the cache
      atomic_store(&cache->lock, STATE_FREE);

   } // end of cache locking
   else
   {
      /* Sleep for 1ms */
      SLEEP_AND_GOTO(1000000L, retry_cache_locking)
   }

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

static int
bad_request(int client_fd)
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
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgagroal_append(data, "HTTP/1.1 400 Bad Request\r\n");
   data = pgagroal_append(data, "Date: ");
   data = pgagroal_append(data, &time_buf[0]);
   data = pgagroal_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static void
general_information(int client_fd)
{
   char* data = NULL;
   struct configuration* config;
   struct prometheus* prometheus;

   config = (struct configuration*)shmem;

   prometheus = (struct prometheus*)prometheus_shmem;

   data = pgagroal_append(data, "#HELP pgagroal_state The state of pgagroal\n");
   data = pgagroal_append(data, "#TYPE pgagroal_state gauge\n");
   data = pgagroal_append(data, "pgagroal_state ");
   if (config->gracefully)
   {
      data = pgagroal_append(data, "2");
   }
   else
   {
      data = pgagroal_append(data, "1");
   }
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_pipeline_mode The mode of pipeline\n");
   data = pgagroal_append(data, "#TYPE pgagroal_pipeline_mode gauge\n");
   data = pgagroal_append(data, "pgagroal_pipeline_mode ");
   data = pgagroal_append_int(data, config->pipeline);
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_server_error The number of errors for servers\n");
   data = pgagroal_append(data, "#TYPE pgagroal_server_error counter\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      int state = atomic_load(&config->servers[i].state);

      data = pgagroal_append(data, "pgagroal_server_error{");

      data = pgagroal_append(data, "name=\"");
      data = pgagroal_append(data, config->servers[i].name);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "state=\"");

      switch (state)
      {
         case SERVER_NOTINIT:
         case SERVER_NOTINIT_PRIMARY:
            data = pgagroal_append(data, "not_init");
            break;
         case SERVER_PRIMARY:
            data = pgagroal_append(data, "primary");
            break;
         case SERVER_REPLICA:
            data = pgagroal_append(data, "replica");
            break;
         case SERVER_FAILOVER:
            data = pgagroal_append(data, "failover");
            break;
         case SERVER_FAILED:
            data = pgagroal_append(data, "failed");
            break;
         default:
            break;
      }

      data = pgagroal_append(data, "\"} ");

      data = pgagroal_append_ulong(data, atomic_load(&prometheus->server_error[i]));
      data = pgagroal_append(data, "\n");
   }
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "#HELP pgagroal_failed_servers The number of failed servers\n");
   data = pgagroal_append(data, "#TYPE pgagroal_failed_servers gauge\n");
   data = pgagroal_append(data, "pgagroal_failed_servers ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->failed_servers));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_wait_time The waiting time of clients\n");
   data = pgagroal_append(data, "#TYPE pgagroal_wait_time gauge\n");
   data = pgagroal_append(data, "pgagroal_wait_time ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->client_wait_time));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_query_count The number of queries\n");
   data = pgagroal_append(data, "#TYPE pgagroal_query_count counter\n");
   data = pgagroal_append(data, "pgagroal_query_count ");
   data = pgagroal_append_ullong(data, atomic_load(&prometheus->query_count));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_query_count The number of queries per connection\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_query_count counter\n");
   for (int i = 0; i < config->max_connections; i++)
   {
      data = pgagroal_append(data, "pgagroal_connection_query_count{");

      data = pgagroal_append(data, "id=\"");
      data = pgagroal_append_int(data, i);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "user=\"");
      data = pgagroal_append(data, config->connections[i].username);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "database=\"");
      data = pgagroal_append(data, config->connections[i].database);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "application_name=\"");
      data = pgagroal_append(data, config->connections[i].appname);
      data = pgagroal_append(data, "\"} ");

      data = pgagroal_append_ullong(data, atomic_load(&prometheus->prometheus_connections[i].query_count));
      data = pgagroal_append(data, "\n");
   }
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "#HELP pgagroal_tx_count The number of transactions\n");
   data = pgagroal_append(data, "#TYPE pgagroal_tx_count counter\n");
   data = pgagroal_append(data, "pgagroal_tx_count ");
   data = pgagroal_append_ullong(data, atomic_load(&prometheus->tx_count));
   data = pgagroal_append(data, "\n\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }
}

static void
connection_information(int client_fd)
{
   char* data = NULL;
   int active;
   int total;
   struct configuration* config;

   config = (struct configuration*)shmem;

   active = 0;
   total = 0;

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);
      switch (state)
      {
         case STATE_IN_USE:
         case STATE_GRACEFULLY:
            active++;
         case STATE_INIT:
         case STATE_FREE:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_MAX_CONNECTION_AGE:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            total++;
            break;
         default:
            break;
      }
   }

   data = pgagroal_append(data, "#HELP pgagroal_active_connections The number of active connections\n");
   data = pgagroal_append(data, "#TYPE pgagroal_active_connections gauge\n");
   data = pgagroal_append(data, "pgagroal_active_connections ");
   data = pgagroal_append_int(data, active);
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_total_connections The total number of connections\n");
   data = pgagroal_append(data, "#TYPE pgagroal_total_connections gauge\n");
   data = pgagroal_append(data, "pgagroal_total_connections ");
   data = pgagroal_append_int(data, total);
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_max_connections The maximum number of connections\n");
   data = pgagroal_append(data, "#TYPE pgagroal_max_connections counter\n");
   data = pgagroal_append(data, "pgagroal_max_connections ");
   data = pgagroal_append_int(data, config->max_connections);
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection The connection information\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection gauge\n");
   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);

      data = pgagroal_append(data, "pgagroal_connection{");

      data = pgagroal_append(data, "id=\"");
      data = pgagroal_append_int(data, i);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "user=\"");
      data = pgagroal_append(data, config->connections[i].username);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "database=\"");
      data = pgagroal_append(data, config->connections[i].database);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "application_name=\"");
      data = pgagroal_append(data, config->connections[i].appname);
      data = pgagroal_append(data, "\",");

      data = pgagroal_append(data, "state=\"");

      switch (state)
      {
         case STATE_NOTINIT:
            data = pgagroal_append(data, "not_init");
            break;
         case STATE_INIT:
            data = pgagroal_append(data, "init");
            break;
         case STATE_FREE:
            data = pgagroal_append(data, "free");
            break;
         case STATE_IN_USE:
            data = pgagroal_append(data, "in_use");
            break;
         case STATE_GRACEFULLY:
            data = pgagroal_append(data, "gracefully");
            break;
         case STATE_FLUSH:
            data = pgagroal_append(data, "flush");
            break;
         case STATE_IDLE_CHECK:
            data = pgagroal_append(data, "idle_check");
            break;
         case STATE_MAX_CONNECTION_AGE:
            data = pgagroal_append(data, "max_connection_age");
            break;
         case STATE_VALIDATION:
            data = pgagroal_append(data, "validation");
            break;
         case STATE_REMOVE:
            data = pgagroal_append(data, "remove");
            break;
         default:
            break;
      }

      data = pgagroal_append(data, "\"} ");

      switch (state)
      {
         case STATE_NOTINIT:
            data = pgagroal_append(data, "0");
            break;
         case STATE_INIT:
         case STATE_FREE:
         case STATE_IN_USE:
         case STATE_GRACEFULLY:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_MAX_CONNECTION_AGE:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            data = pgagroal_append(data, "1");
            break;
         default:
            break;
      }

      data = pgagroal_append(data, "\n");

      if (strlen(data) > CHUNK_SIZE)
      {
         send_chunk(client_fd, data);
         metrics_cache_append(data);
         free(data);
         data = NULL;
      }
   }

   data = pgagroal_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }
}

static void
limit_information(int client_fd)
{
   char* data = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->number_of_limits > 0)
   {
      data = pgagroal_append(data, "#HELP pgagroal_limit The limit information\n");
      data = pgagroal_append(data, "#TYPE pgagroal_limit gauge\n");
      for (int i = 0; i < config->number_of_limits; i++)
      {
         data = pgagroal_append(data, "pgagroal_limit{");

         data = pgagroal_append(data, "user=\"");
         data = pgagroal_append(data, config->limits[i].username);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "database=\"");
         data = pgagroal_append(data, config->limits[i].database);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "type=\"min\"} ");
         data = pgagroal_append_int(data, config->limits[i].min_size);
         data = pgagroal_append(data, "\n");

         data = pgagroal_append(data, "pgagroal_limit{");

         data = pgagroal_append(data, "user=\"");
         data = pgagroal_append(data, config->limits[i].username);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "database=\"");
         data = pgagroal_append(data, config->limits[i].database);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "type=\"initial\"} ");
         data = pgagroal_append_int(data, config->limits[i].initial_size);
         data = pgagroal_append(data, "\n");

         data = pgagroal_append(data, "pgagroal_limit{");

         data = pgagroal_append(data, "user=\"");
         data = pgagroal_append(data, config->limits[i].username);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "database=\"");
         data = pgagroal_append(data, config->limits[i].database);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "type=\"max\"} ");
         data = pgagroal_append_int(data, config->limits[i].max_size);
         data = pgagroal_append(data, "\n");

         data = pgagroal_append(data, "pgagroal_limit{");

         data = pgagroal_append(data, "user=\"");
         data = pgagroal_append(data, config->limits[i].username);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "database=\"");
         data = pgagroal_append(data, config->limits[i].database);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "type=\"active\"} ");
         data = pgagroal_append_int(data, config->limits[i].active_connections);
         data = pgagroal_append(data, "\n");

         if (strlen(data) > CHUNK_SIZE)
         {
            send_chunk(client_fd, data);
            metrics_cache_append(data);
            free(data);
            data = NULL;
         }
      }

      data = pgagroal_append(data, "\n");

      if (data != NULL)
      {
         send_chunk(client_fd, data);
         metrics_cache_append(data);
         free(data);
         data = NULL;
      }
   }
}

static void
session_information(int client_fd)
{
   char* data = NULL;
   unsigned long counter;
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   counter = 0;

   data = pgagroal_append(data, "#HELP pgagroal_session_time_seconds The session times\n");
   data = pgagroal_append(data, "#TYPE pgagroal_session_time_seconds histogram\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"5\"} ");
   counter += atomic_load(&prometheus->session_time[0]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"10\"} ");
   counter += atomic_load(&prometheus->session_time[1]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"20\"} ");
   counter += atomic_load(&prometheus->session_time[2]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"30\"} ");
   counter += atomic_load(&prometheus->session_time[3]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"45\"} ");
   counter += atomic_load(&prometheus->session_time[4]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"60\"} ");
   counter += atomic_load(&prometheus->session_time[5]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"300\"} ");
   counter += atomic_load(&prometheus->session_time[6]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"600\"} ");
   counter += atomic_load(&prometheus->session_time[7]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"1200\"} ");
   counter += atomic_load(&prometheus->session_time[8]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"1800\"} ");
   counter += atomic_load(&prometheus->session_time[9]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"2700\"} ");
   counter += atomic_load(&prometheus->session_time[10]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"3600\"} ");
   counter += atomic_load(&prometheus->session_time[11]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"7200\"} ");
   counter += atomic_load(&prometheus->session_time[12]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"14400\"} ");
   counter += atomic_load(&prometheus->session_time[13]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"21600\"} ");
   counter += atomic_load(&prometheus->session_time[14]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"43200\"} ");
   counter += atomic_load(&prometheus->session_time[15]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"86400\"} ");
   counter += atomic_load(&prometheus->session_time[16]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_bucket{le=\"+Inf\"} ");
   counter += atomic_load(&prometheus->session_time[17]);
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_sum ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->session_time_sum));
   data = pgagroal_append(data, "\n");

   data = pgagroal_append(data, "pgagroal_session_time_seconds_count ");
   data = pgagroal_append_ulong(data, counter);
   data = pgagroal_append(data, "\n\n");

   send_chunk(client_fd, data);
   metrics_cache_append(data);
   free(data);
   data = NULL;
}

static void
pool_information(int client_fd)
{
   char* data = NULL;
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   data = pgagroal_append(data, "#HELP pgagroal_connection_error Number of connection errors\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_error counter\n");
   data = pgagroal_append(data, "pgagroal_connection_error ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_error));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_kill Number of connection kills\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_kill counter\n");
   data = pgagroal_append(data, "pgagroal_connection_kill ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_kill));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_remove Number of connection removes\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_remove counter\n");
   data = pgagroal_append(data, "pgagroal_connection_remove ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_remove));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_timeout Number of connection time outs\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_timeout counter\n");
   data = pgagroal_append(data, "pgagroal_connection_timeout ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_timeout));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_return Number of connection returns\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_return counter\n");
   data = pgagroal_append(data, "pgagroal_connection_return ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_return));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_invalid Number of connection invalids\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_invalid counter\n");
   data = pgagroal_append(data, "pgagroal_connection_invalid ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_invalid));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_get Number of connection gets\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_get counter\n");
   data = pgagroal_append(data, "pgagroal_connection_get ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_get));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_idletimeout Number of connection idle timeouts\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_idletimeout counter\n");
   data = pgagroal_append(data, "pgagroal_connection_idletimeout ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_idletimeout));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_max_connection_age Number of connection max age timeouts\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_max_connection_age counter\n");
   data = pgagroal_append(data, "pgagroal_connection_max_connection_age ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_max_connection_age));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_flush Number of connection flushes\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_flush counter\n");
   data = pgagroal_append(data, "pgagroal_connection_flush ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_flush));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_connection_success Number of connection successes\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_success counter\n");
   data = pgagroal_append(data, "pgagroal_connection_success ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connection_success));
   data = pgagroal_append(data, "\n\n");

   send_chunk(client_fd, data);
   metrics_cache_append(data);
   free(data);
   data = NULL;
}

static void
auth_information(int client_fd)
{
   char* data = NULL;
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   data = pgagroal_append(data, "#HELP pgagroal_auth_user_success Number of successful user authentications\n");
   data = pgagroal_append(data, "#TYPE pgagroal_auth_user_success counter\n");
   data = pgagroal_append(data, "pgagroal_auth_user_success ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->auth_user_success));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_auth_user_bad_password Number of bad passwords during user authentication\n");
   data = pgagroal_append(data, "#TYPE pgagroal_auth_user_bad_password counter\n");
   data = pgagroal_append(data, "pgagroal_auth_user_bad_password ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->auth_user_bad_password));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_auth_user_error Number of errors during user authentication\n");
   data = pgagroal_append(data, "#TYPE pgagroal_auth_user_error counter\n");
   data = pgagroal_append(data, "pgagroal_auth_user_error ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->auth_user_error));
   data = pgagroal_append(data, "\n\n");

   send_chunk(client_fd, data);
   metrics_cache_append(data);
   free(data);
   data = NULL;
}

static void
client_information(int client_fd)
{
   char* data = NULL;
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   data = pgagroal_append(data, "#HELP pgagroal_client_wait Number of waiting clients\n");
   data = pgagroal_append(data, "#TYPE pgagroal_client_wait gauge\n");
   data = pgagroal_append(data, "pgagroal_client_wait ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->client_wait));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_client_active Number of active clients\n");
   data = pgagroal_append(data, "#TYPE pgagroal_client_active gauge\n");
   data = pgagroal_append(data, "pgagroal_client_active ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->client_active));
   data = pgagroal_append(data, "\n\n");

   send_chunk(client_fd, data);
   metrics_cache_append(data);
   free(data);
   data = NULL;
}

static void
internal_information(int client_fd)
{
   char* data = NULL;
   struct prometheus* prometheus;

   prometheus = (struct prometheus*)prometheus_shmem;

   data = pgagroal_append(data, "#HELP pgagroal_network_sent Bytes sent by clients\n");
   data = pgagroal_append(data, "#TYPE pgagroal_network_sent gauge\n");
   data = pgagroal_append(data, "pgagroal_network_sent ");
   data = pgagroal_append_ullong(data, atomic_load(&prometheus->network_sent));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_network_received Bytes received from servers\n");
   data = pgagroal_append(data, "#TYPE pgagroal_network_received gauge\n");
   data = pgagroal_append(data, "pgagroal_network_received ");
   data = pgagroal_append_ullong(data, atomic_load(&prometheus->network_received));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_client_sockets Number of sockets the client used\n");
   data = pgagroal_append(data, "#TYPE pgagroal_client_sockets gauge\n");
   data = pgagroal_append(data, "pgagroal_client_sockets ");
   data = pgagroal_append_int(data, atomic_load(&prometheus->client_sockets));
   data = pgagroal_append(data, "\n\n");

   data = pgagroal_append(data, "#HELP pgagroal_self_sockets Number of sockets used by pgagroal itself\n");
   data = pgagroal_append(data, "#TYPE pgagroal_self_sockets gauge\n");
   data = pgagroal_append(data, "pgagroal_self_sockets ");
   data = pgagroal_append_int(data, atomic_load(&prometheus->self_sockets));
   data = pgagroal_append(data, "\n\n");

   send_chunk(client_fd, data);
   metrics_cache_append(data);
   free(data);
   data = NULL;
}

/**
 * Provides information about the connection awaiting.
 *
 * Prints the total connection awaiting counter
 * and also one line per limit if there are limits.
 */
static void
connection_awaiting_information(int client_fd)
{
   char* data = NULL;
   struct configuration* config;
   struct prometheus* prometheus;

   config = (struct configuration*)shmem;

   prometheus = (struct prometheus*)prometheus_shmem;

   data = pgagroal_append(data, "#HELP pgagroal_connection_awaiting Number of connection on-hold (awaiting)\n");
   data = pgagroal_append(data, "#TYPE pgagroal_connection_awaiting gauge\n");
   data = pgagroal_append(data, "pgagroal_connection_awaiting ");
   data = pgagroal_append_ulong(data, atomic_load(&prometheus->connections_awaiting_total));
   data = pgagroal_append(data, "\n\n");

   if (config->number_of_limits > 0)
   {
      data = pgagroal_append(data, "#HELP pgagroal_limit_awaiting The connections on-hold (awaiting) information\n");
      data = pgagroal_append(data, "#TYPE pgagroal_limit_awaiting gauge\n");
      for (int i = 0; i < config->number_of_limits; i++)
      {

         data = pgagroal_append(data, "pgagroal_limit_awaiting{");

         data = pgagroal_append(data, "user=\"");
         data = pgagroal_append(data, config->limits[i].username);
         data = pgagroal_append(data, "\",");

         data = pgagroal_append(data, "database=\"");
         data = pgagroal_append(data, config->limits[i].database);
         data = pgagroal_append(data, "\"} ");

         data = pgagroal_append_ulong(data, atomic_load(&prometheus->connections_awaiting[i]));
         data = pgagroal_append(data, "\n");

         if (strlen(data) > CHUNK_SIZE)
         {
            send_chunk(client_fd, data);
            metrics_cache_append(data);
            free(data);
            data = NULL;
         }

      }
   }

   if (data != NULL)
   {
      data = pgagroal_append(data, "\n");
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }
}

static int
send_chunk(int client_fd, char* data)
{
   int status;
   char* m = NULL;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   m = calloc(1, 20);
   if (m == NULL)
   {
      pgagroal_log_fatal("Couldn't allocate memory while binding host");
      return MESSAGE_STATUS_ERROR;
   }

   sprintf(m, "%lX\r\n", strlen(data));

   m = pgagroal_append(m, data);
   m = pgagroal_append(m, "\r\n");

   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgagroal_write_message(NULL, client_fd, &msg);

   free(m);

   return status;
}

/**
 * Checks if the Prometheus cache configuration setting
 * (`metrics_cache`) has a non-zero value, that means there
 * are seconds to cache the response.
 *
 * @return true if there is a cache configuration,
 *         false if no cache is active
 */
static bool
is_metrics_cache_configured(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   // cannot have caching if not set metrics!
   if (config->metrics == 0)
   {
      return false;
   }

   return config->metrics_cache_max_age != PGAGROAL_PROMETHEUS_CACHE_DISABLED;
}

/**
 * Checks if the cache is still valid, and therefore can be
 * used to serve as a response.
 * A cache is considred valid if it has non-empty payload and
 * a timestamp in the future.
 *
 * @return true if the cache is still valid
 */
static bool
is_metrics_cache_valid(void)
{
   time_t now;

   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   if (cache->valid_until == 0 || strlen(cache->data) == 0)
   {
      return false;
   }

   now = time(NULL);
   return now <= cache->valid_until;
}

int
pgagroal_init_prometheus_cache(size_t* p_size, void** p_shmem)
{
   struct prometheus_cache* cache;
   struct configuration* config;
   size_t cache_size = 0;
   size_t struct_size = 0;

   config = (struct configuration*)shmem;

   // first of all, allocate the overall cache structure
   cache_size = metrics_cache_size_to_alloc();
   struct_size = sizeof(struct prometheus_cache);

   if (pgagroal_create_shared_memory(struct_size + cache_size, config->hugepage, (void*) &cache))
   {
      goto error;
   }

   memset(cache, 0, struct_size + cache_size);
   cache->valid_until = 0;
   cache->size = cache_size;
   atomic_init(&cache->lock, STATE_FREE);

   // success! do the memory swap
   *p_shmem = cache;
   *p_size = cache_size + struct_size;
   return 0;

error:
   // disable caching
   config->metrics_cache_max_age = config->metrics_cache_max_size = PGAGROAL_PROMETHEUS_CACHE_DISABLED;
   pgagroal_log_error("Cannot allocate shared memory for the Prometheus cache!");
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}

/**
 * Provides the size of the cache to allocate.
 *
 * It checks if the metrics cache is configured, and
 * computers the right minimum value between the
 * user configured requested size and the default
 * cache size.
 *
 * @return the cache size to allocate
 */
static size_t
metrics_cache_size_to_alloc(void)
{
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   // which size to use ?
   // either the configured (i.e., requested by user) if lower than the max size
   // or the default value
   if (is_metrics_cache_configured())
   {
      cache_size = config->metrics_cache_max_size > 0
            ? MIN(config->metrics_cache_max_size, PROMETHEUS_MAX_CACHE_SIZE)
            : PROMETHEUS_DEFAULT_CACHE_SIZE;
   }

   return cache_size;
}

/**
 * Invalidates the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * Invalidating the cache means that the payload is zero-filled
 * and that the valid_until field is set to zero too.
 */
static void
metrics_cache_invalidate(void)
{
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   memset(cache->data, 0, cache->size);
   cache->valid_until = 0;
}

/**
 * Appends data to the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * If the input data is empty, nothing happens.
 * The data is appended only if the cache does not overflows, that
 * means the current size of the cache plus the size of the data
 * to append does not exceed the current cache size.
 * If the cache overflows, the cache is flushed and marked
 * as invalid.
 * This makes safe to call this method along the workflow of
 * building the Prometheus response.
 *
 * @param data the string to append to the cache
 * @return true on success
 */
static bool
metrics_cache_append(char* data)
{
   int origin_length = 0;
   int append_length = 0;
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   if (!is_metrics_cache_configured())
   {
      return false;
   }

   origin_length = strlen(cache->data);
   append_length = strlen(data);
   // need to append the data to the cache
   if (origin_length + append_length >= cache->size)
   {
      // cannot append new data, so invalidate cache
      pgagroal_log_debug("Cannot append %d bytes to the Prometheus cache because it will overflow the size of %d bytes (currently at %d bytes). HINT: try adjusting `metrics_cache_max_size`",
                         append_length,
                         cache->size,
                         origin_length);
      metrics_cache_invalidate();
      return false;
   }

   // append the data to the data field
   memcpy(cache->data + origin_length, data, append_length);
   cache->data[origin_length + append_length + 1] = '\0';
   return true;
}

/**
 * Finalizes the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * This method should be invoked when the cache is complete
 * and therefore can be served.
 *
 * @return true if the cache has a validity
 */
static bool
metrics_cache_finalize(void)
{
   struct configuration* config;
   struct prometheus_cache* cache;
   time_t now;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;
   config = (struct configuration*)shmem;

   if (!is_metrics_cache_configured())
   {
      return false;
   }

   now = time(NULL);
   cache->valid_until = now + config->metrics_cache_max_age;
   return cache->valid_until > now;
}
