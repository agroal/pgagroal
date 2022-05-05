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
#include <network.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <pool.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <tracker.h>
#include <utils.h>

/* system */
#include <assert.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int find_best_rule(char* username, char* database);
static bool remove_connection(char* username, char* database);
static void connection_details(int slot);
static bool do_prefill(char* username, char* database, int size);

int
pgagroal_get_connection(char* username, char* database, bool reuse, bool transaction_mode, int* slot, SSL** ssl)
{
   bool do_init;
   bool has_lock;
   int connections;
   signed char not_init;
   signed char free;
   int server;
   int fd;
   time_t start_time;
   int best_rule;
   int retries;
   int ret;

   struct configuration* config;
   struct prometheus* prometheus;

   config = (struct configuration*)shmem;
   prometheus = (struct prometheus*)prometheus_shmem;

   pgagroal_prometheus_connection_get();

   best_rule = find_best_rule(username, database);
   retries = 0;
   start_time = time(NULL);

start:

   *slot = -1;
   *ssl = NULL;
   do_init = false;
   has_lock = false;

   if (best_rule >= 0)
   {
      connections = atomic_fetch_add(&config->limits[best_rule].active_connections, 1);
      if (connections >= config->limits[best_rule].max_size)
      {
         goto retry;
      }
   }

   connections = atomic_fetch_add(&config->active_connections, 1);
   has_lock = true;
   if (connections >= config->max_connections)
   {
      goto retry;
   }

   /* Try and find an existing free connection */
   if (reuse)
   {
      for (int i = 0; *slot == -1 && i < config->max_connections; i++)
      {
         free = STATE_FREE;

         if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_IN_USE))
         {
            if (best_rule == config->connections[i].limit_rule &&
                !strcmp((const char*)(&config->connections[i].username), username) &&
                !strcmp((const char*)(&config->connections[i].database), database))
            {
               *slot = i;
            }
            else
            {
               atomic_store(&config->states[i], STATE_FREE);
            }
         }
      }
   }

   if (*slot == -1 && !transaction_mode)
   {
      /* Ok, try and create a new connection */
      for (int i = 0; *slot == -1 && i < config->max_connections; i++)
      {
         not_init = STATE_NOTINIT;

         if (atomic_compare_exchange_strong(&config->states[i], &not_init, STATE_INIT))
         {
            *slot = i;
            do_init = true;
         }
      }
   }

   if (*slot != -1)
   {
      config->connections[*slot].limit_rule = best_rule;
      config->connections[*slot].pid = getpid();

      if (do_init)
      {
         /* We need to find the server for the connection */
         if (pgagroal_get_primary(&server))
         {
            config->connections[*slot].limit_rule = -1;
            config->connections[*slot].pid = -1;
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            if (!fork())
            {
               pgagroal_flush(FLUSH_GRACEFULLY, "*");
            }

            goto error;
         }

         pgagroal_log_debug("connect: server %d", server);

         if (config->servers[server].host[0] == '/')
         {
            char pgsql[MISC_LENGTH];

            memset(&pgsql, 0, sizeof(pgsql));
            snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
            ret = pgagroal_connect_unix_socket(config->servers[server].host, &pgsql[0], &fd);
         }
         else
         {
            ret = pgagroal_connect(config->servers[server].host, config->servers[server].port, &fd);
         }

         if (ret)
         {
            pgagroal_log_error("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
            config->connections[*slot].limit_rule = -1;
            config->connections[*slot].pid = -1;
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            pgagroal_prometheus_server_error(server);

            if (!fork())
            {
               pgagroal_flush_server(server);
            }

            if (config->failover)
            {
               pgagroal_server_force_failover(server);
               pgagroal_prometheus_failed_servers();
               goto retry;
            }

            goto error;
         }

         pgagroal_log_debug("connect: %s:%d using slot %d fd %d", config->servers[server].host, config->servers[server].port, *slot, fd);

         config->connections[*slot].server = server;

         memset(&config->connections[*slot].username, 0, MAX_USERNAME_LENGTH);
         memcpy(&config->connections[*slot].username, username, MIN(strlen(username), MAX_USERNAME_LENGTH - 1));

         memset(&config->connections[*slot].database, 0, MAX_DATABASE_LENGTH);
         memcpy(&config->connections[*slot].database, database, MIN(strlen(database), MAX_DATABASE_LENGTH - 1));

         config->connections[*slot].has_security = SECURITY_INVALID;
         config->connections[*slot].fd = fd;

         atomic_store(&config->states[*slot], STATE_IN_USE);
      }
      else
      {
         bool kill = false;

         /* Verify the socket for the slot */
         if (!pgagroal_socket_isvalid(config->connections[*slot].fd))
         {
            if (!transaction_mode)
            {
               kill = true;
            }
            else
            {
               atomic_store(&config->states[*slot], STATE_FREE);
               goto retry;
            }
         }

         if (!kill && config->validation == VALIDATION_FOREGROUND)
         {
            kill = !pgagroal_connection_isvalid(config->connections[*slot].fd);
         }

         if (kill)
         {
            int status;

            pgagroal_log_debug("pgagroal_get_connection: Slot %d FD %d - Error", *slot, config->connections[*slot].fd);
            pgagroal_tracking_event_slot(TRACKER_BAD_CONNECTION, *slot);
            status = pgagroal_kill_connection(*slot, *ssl);

            if (config->number_of_users > 0 && config->number_of_limits > 0)
            {
               if (!fork())
               {
                  pgagroal_prefill(false);
               }
            }

            if (status == 0)
            {
               goto retry2;
            }
            else
            {
               goto timeout;
            }
         }
      }

      config->connections[*slot].timestamp = time(NULL);

      atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
      pgagroal_prometheus_connection_success();
      pgagroal_tracking_event_slot(TRACKER_GET_CONNECTION_SUCCESS, *slot);

      return 0;
   }
   else
   {
retry:
      if (best_rule >= 0)
      {
         atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
      }
      if (has_lock)
      {
         atomic_fetch_sub(&config->active_connections, 1);
      }
retry2:
      if (config->blocking_timeout > 0)
      {
         /* Sleep for 500ms */
         struct timespec ts;
         ts.tv_sec = 0;
         ts.tv_nsec = 500000000L;
         nanosleep(&ts, NULL);

         double diff = difftime(time(NULL), start_time);
         if (diff >= (double)config->blocking_timeout)
         {
            goto timeout;
         }

         if (best_rule == -1)
         {
            remove_connection(username, database);
         }

         goto start;
      }
      else
      {
         if (!transaction_mode)
         {
            if (best_rule == -1)
            {
               if (remove_connection(username, database))
               {
                  if (retries < config->max_retries)
                  {
                     retries++;
                     goto start;
                  }
               }
            }
            else
            {
               if (retries < config->max_retries)
               {
                  retries++;
                  goto start;
               }
            }
         }
         else
         {
            /* Sleep for 1000 nanos */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000L;
            nanosleep(&ts, NULL);

            goto start;
         }
      }
   }

timeout:
   atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
   pgagroal_prometheus_connection_timeout();
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_TIMEOUT, username, database);

   return 1;

error:
   if (best_rule >= 0)
   {
      atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
   }
   atomic_fetch_sub(&config->active_connections, 1);
   atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
   pgagroal_prometheus_connection_error();
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_ERROR, username, database);

   return 2;
}

int
pgagroal_return_connection(int slot, SSL* ssl, bool transaction_mode)
{
   int state;
   struct configuration* config;

   config = (struct configuration*)shmem;

   /* Verify the socket for the slot */
   if (!transaction_mode && !pgagroal_socket_isvalid(config->connections[slot].fd))
   {
      pgagroal_log_debug("pgagroal_return_connection: Slot %d FD %d - Error", slot, config->connections[slot].fd);
      config->connections[slot].has_security = SECURITY_INVALID;
   }

   /* Can we cache this connection ? */
   if (config->connections[slot].has_security != SECURITY_INVALID &&
       (config->connections[slot].has_security != SECURITY_SCRAM256 ||
        (config->connections[slot].has_security == SECURITY_SCRAM256 &&
         (config->authquery || pgagroal_user_known(config->connections[slot].username)))) &&
       ssl == NULL)
   {
      state = atomic_load(&config->states[slot]);

      /* Return the connection, if not GRACEFULLY */
      if (state == STATE_IN_USE)
      {
         pgagroal_log_debug("pgagroal_return_connection: Slot %d FD %d", slot, config->connections[slot].fd);

         if (!transaction_mode)
         {
            if (pgagroal_write_discard_all(ssl, config->connections[slot].fd))
            {
               goto kill_connection;
            }
         }

         pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_SUCCESS, slot);

         config->connections[slot].timestamp = time(NULL);

         if (config->connections[slot].new)
         {
            pgagroal_management_transfer_connection(slot);
         }

         pgagroal_management_return_connection(slot);

         if (config->connections[slot].limit_rule >= 0)
         {
            atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].active_connections, 1);
         }

         config->connections[slot].new = false;
         config->connections[slot].pid = -1;
         config->connections[slot].tx_mode = transaction_mode;
         memset(&config->connections[slot].appname, 0, sizeof(config->connections[slot].appname));
         atomic_store(&config->states[slot], STATE_FREE);
         atomic_fetch_sub(&config->active_connections, 1);

         pgagroal_prometheus_connection_return();

         return 0;
      }
      else if (state == STATE_GRACEFULLY)
      {
         pgagroal_write_terminate(ssl, config->connections[slot].fd);
      }
   }

kill_connection:

   pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_KILL, slot);

   return pgagroal_kill_connection(slot, ssl);
}

int
pgagroal_kill_connection(int slot, SSL* ssl)
{
   SSL_CTX* ctx;
   int ssl_shutdown;
   int result = 0;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_log_debug("pgagroal_kill_connection: Slot %d FD %d State %d PID %d",
                      slot, config->connections[slot].fd, atomic_load(&config->states[slot]),
                      config->connections[slot].pid);

   pgagroal_tracking_event_slot(TRACKER_KILL_CONNECTION, slot);

   fd = config->connections[slot].fd;
   if (fd != -1)
   {
      pgagroal_management_kill_connection(slot, fd);

      if (ssl != NULL)
      {
         ctx = SSL_get_SSL_CTX(ssl);
         ssl_shutdown = SSL_shutdown(ssl);
         if (ssl_shutdown == 0)
         {
            SSL_shutdown(ssl);
         }
         SSL_free(ssl);
         SSL_CTX_free(ctx);
      }

      if (!pgagroal_socket_has_error(fd))
      {
         pgagroal_disconnect(fd);
      }
   }
   else
   {
      result = 1;
   }

   if (config->connections[slot].pid != -1)
   {
      if (config->connections[slot].limit_rule >= 0)
      {
         atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].active_connections, 1);
      }

      atomic_fetch_sub(&config->active_connections, 1);
   }

   memset(&config->connections[slot].username, 0, sizeof(config->connections[slot].username));
   memset(&config->connections[slot].database, 0, sizeof(config->connections[slot].database));
   memset(&config->connections[slot].appname, 0, sizeof(config->connections[slot].appname));

   config->connections[slot].new = true;
   config->connections[slot].server = -1;
   config->connections[slot].tx_mode = false;

   config->connections[slot].has_security = SECURITY_INVALID;
   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      config->connections[slot].security_lengths[i] = 0;
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   config->connections[slot].backend_pid = 0;
   config->connections[slot].backend_secret = 0;

   config->connections[slot].limit_rule = -1;
   config->connections[slot].timestamp = -1;
   config->connections[slot].fd = -1;
   config->connections[slot].pid = -1;

   atomic_store(&config->states[slot], STATE_NOTINIT);

   pgagroal_prometheus_connection_kill();

   return result;
}

void
pgagroal_idle_timeout(void)
{
   bool prefill;
   time_t now;
   signed char free;
   signed char idle_check;
   struct configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct configuration*)shmem;
   now = time(NULL);
   prefill = false;

   pgagroal_log_debug("pgagroal_idle_timeout");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      idle_check = STATE_IDLE_CHECK;

      if (atomic_compare_exchange_strong(&config->states[i], &free, idle_check))
      {
         double diff = difftime(now, config->connections[i].timestamp);
         if (diff >= (double)config->idle_timeout && !config->connections[i].tx_mode)
         {
            pgagroal_prometheus_connection_idletimeout();
            pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &idle_check, STATE_FREE))
            {
               pgagroal_prometheus_connection_idletimeout();
               pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill && config->number_of_users > 0 && config->number_of_limits > 0)
   {
      if (!fork())
      {
         pgagroal_prefill(false);
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_validation(void)
{
   bool prefill = true;
   time_t now;
   signed char free;
   signed char validation;
   struct configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct configuration*)shmem;
   now = time(NULL);

   pgagroal_log_debug("pgagroal_validation");

   /* We run backwards */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      validation = STATE_VALIDATION;

      if (atomic_compare_exchange_strong(&config->states[i], &free, validation))
      {
         bool kill = false;
         double diff;

         /* Verify the socket for the slot */
         if (!pgagroal_socket_isvalid(config->connections[i].fd))
         {
            kill = true;
         }

         /* While we have the connection in validation may as well check for idle_timeout */
         if (!kill && config->idle_timeout > 0)
         {
            diff = difftime(now, config->connections[i].timestamp);
            if (diff >= (double)config->idle_timeout)
            {
               kill = true;
            }
         }

         /* Ok, send SELECT 1 */
         if (!kill)
         {
            kill = !pgagroal_connection_isvalid(config->connections[i].fd);
         }

         if (kill)
         {
            pgagroal_prometheus_connection_invalid();
            pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &validation, STATE_FREE))
            {
               pgagroal_prometheus_connection_invalid();
               pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill && config->number_of_users > 0 && config->number_of_limits > 0)
   {
      if (!fork())
      {
         pgagroal_prefill(false);
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_flush(int mode, char* database)
{
   bool prefill;
   signed char free;
   signed char in_use;
   bool do_kill;
   signed char server_state;
   struct configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct configuration*)shmem;

   prefill = false;

   pgagroal_log_debug("pgagroal_flush");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      in_use = STATE_IN_USE;
      do_kill = false;

      if (config->connections[i].server != -1)
      {
         server_state = atomic_load(&config->servers[config->connections[i].server].state);
         if (server_state == SERVER_FAILED)
         {
            do_kill = true;
         }
      }

      if (!do_kill)
      {
         bool consider = false;

         if (!strcmp(database, "*") || !strcmp(config->connections[i].database, database))
         {
            consider = true;
         }

         if (consider)
         {
            if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_FLUSH))
            {
               if (pgagroal_socket_isvalid(config->connections[i].fd))
               {
                  pgagroal_write_terminate(NULL, config->connections[i].fd);
               }
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
            else if (mode == FLUSH_ALL || mode == FLUSH_GRACEFULLY)
            {
               if (atomic_compare_exchange_strong(&config->states[i], &in_use, STATE_FLUSH))
               {
                  if (mode == FLUSH_ALL)
                  {
                     kill(config->connections[i].pid, SIGQUIT);
                     pgagroal_prometheus_connection_flush();
                     pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
                     pgagroal_kill_connection(i, NULL);
                     prefill = true;
                  }
                  else if (mode == FLUSH_GRACEFULLY)
                  {
                     atomic_store(&config->states[i], STATE_GRACEFULLY);
                  }
               }
            }
         }
      }
      else
      {
         switch (atomic_load(&config->states[i]))
         {
            case STATE_NOTINIT:
            case STATE_INIT:
               /* Do nothing */
               break;
            case STATE_FREE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
               break;
            case STATE_IN_USE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            case STATE_IDLE_CHECK:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            default:
               break;
         }
      }
   }

   if (prefill && config->number_of_users > 0 && config->number_of_limits > 0)
   {
      if (!fork())
      {
         pgagroal_prefill(false);
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_flush_server(signed char server)
{
   struct configuration* config;
   int primary = -1;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct configuration*)shmem;

   pgagroal_log_debug("pgagroal_flush_server %s", config->servers[server].name);
   for (int i = 0; i < config->max_connections; i++)
   {
      if (config->connections[i].server == server)
      {
         switch (atomic_load(&config->states[i]))
         {
            case STATE_NOTINIT:
            case STATE_INIT:
               /* Do nothing */
               break;
            case STATE_FREE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               if (pgagroal_socket_isvalid(config->connections[i].fd))
               {
                  pgagroal_write_terminate(NULL, config->connections[i].fd);
               }
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               break;
            case STATE_IN_USE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            case STATE_IDLE_CHECK:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            default:
               break;
         }
      }
   }

   if (pgagroal_get_primary(&primary))
      pgagroal_log_debug("No primary defined");
   else
   {
      if (config->number_of_users > 0 && config->number_of_limits > 0 && server != (unsigned char)primary && primary != -1)
      {
         if (!fork())
         {
            pgagroal_prefill(true);
         }
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_prefill(bool initial)
{
   struct configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct configuration*)shmem;

   pgagroal_log_debug("pgagroal_prefill");

   for (int i = 0; i < config->number_of_limits; i++)
   {
      int size;

      if (initial)
      {
         size = config->limits[i].initial_size;
      }
      else
      {
         size = config->limits[i].min_size;
      }

      if (size > 0)
      {
         if (strcmp("all", config->limits[i].database) && strcmp("all", config->limits[i].username))
         {
            int user = -1;

            for (int j = 0; j < config->number_of_users && user == -1; j++)
            {
               if (!strcmp(config->limits[i].username, config->users[j].username))
               {
                  user = j;
               }
            }

            if (user != -1)
            {
               while (do_prefill(config->users[user].username, config->limits[i].database, size))
               {
                  int32_t slot = -1;
                  SSL* ssl = NULL;

                  if (pgagroal_prefill_auth(config->users[user].username, config->users[user].password,
                                            config->limits[i].database, &slot, &ssl) != AUTH_SUCCESS)
                  {
                     pgagroal_log_warn("Invalid data for user '%s' using limit entry (%d)", config->limits[i].username, i + 1);

                     if (slot != -1)
                     {
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot);
                        pgagroal_kill_connection(slot, ssl);
                     }

                     break;
                  }

                  if (slot != -1)
                  {
                     if (config->connections[slot].has_security != SECURITY_INVALID)
                     {
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_RETURN, slot);
                        pgagroal_return_connection(slot, ssl, false);
                     }
                     else
                     {
                        pgagroal_log_warn("Unsupported security model during prefill for user '%s' using limit entry (%d)", config->limits[i].username, i + 1);
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot);
                        pgagroal_kill_connection(slot, ssl);
                        break;
                     }
                  }
               }
            }
            else
            {
               pgagroal_log_warn("Unknown user '%s' for limit entry (%d)", config->limits[i].username, i + 1);
            }
         }
         else
         {
            pgagroal_log_warn("Limit entry (%d) with invalid definition", i + 1);
         }
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

int
pgagroal_pool_init(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   /* States */
   for (int i = 0; i < MAX_NUMBER_OF_CONNECTIONS; i++)
   {
      atomic_init(&config->states[i], STATE_NOTINIT);
   }

   /* Connections */
   for (int i = 0; i < config->max_connections; i++)
   {
      config->connections[i].new = true;
      config->connections[i].tx_mode = false;
      config->connections[i].server = -1;
      config->connections[i].has_security = SECURITY_INVALID;
      config->connections[i].limit_rule = -1;
      config->connections[i].timestamp = -1;
      config->connections[i].fd = -1;
      config->connections[i].pid = -1;
   }

   return 0;
}

int
pgagroal_pool_shutdown(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);

      if (state != STATE_NOTINIT)
      {
         if (state == STATE_FREE)
         {
            if (pgagroal_socket_isvalid(config->connections[i].fd))
            {
               pgagroal_write_terminate(NULL, config->connections[i].fd);
            }
         }
         pgagroal_disconnect(config->connections[i].fd);

         if (config->connections[i].pid != -1)
         {
            kill(config->connections[i].pid, SIGQUIT);
         }

         atomic_store(&config->states[i], STATE_NOTINIT);
      }
   }

   return 0;
}

int
pgagroal_pool_status(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_log_debug("pgagroal_pool_status: %d/%d", atomic_load(&config->active_connections), config->max_connections);

   for (int i = 0; i < config->max_connections; i++)
   {
      connection_details(i);
   }

#ifdef DEBUG
   assert(atomic_load(&config->active_connections) <= config->max_connections);
#endif

   return 0;
}

static int
find_best_rule(char* username, char* database)
{
   int best_rule;
   struct configuration* config;

   best_rule = -1;
   config = (struct configuration*)shmem;

   if (config->number_of_limits > 0)
   {
      for (int i = 0; i < config->number_of_limits; i++)
      {
         /* There is a match */
         if ((!strcmp("all", config->limits[i].username) || !strcmp(username, config->limits[i].username)) &&
             (!strcmp("all", config->limits[i].database) || !strcmp(database, config->limits[i].database)))
         {
            if (best_rule == -1)
            {
               best_rule = i;
            }
            else
            {
               if (!strcmp(username, config->limits[best_rule].username) &&
                   !strcmp(database, config->limits[best_rule].database))
               {
                  /* We have a precise rule already */
               }
               else if (!strcmp("all", config->limits[best_rule].username))
               {
                  /* User is better */
                  if (strcmp("all", config->limits[i].username))
                  {
                     best_rule = i;
                  }
               }
               else if (!strcmp("all", config->limits[best_rule].database))
               {
                  /* Database is better */
                  if (strcmp("all", config->limits[i].database))
                  {
                     best_rule = i;
                  }
               }
            }
         }
      }
   }

   return best_rule;
}

static bool
remove_connection(char* username, char* database)
{
   signed char free;
   signed char remove;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_log_trace("remove_connection");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      remove = STATE_REMOVE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, remove))
      {
         if (!strcmp(username, config->connections[i].username) && !strcmp(database, config->connections[i].database))
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &remove, STATE_FREE))
            {
               pgagroal_prometheus_connection_remove();
               pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i);
               pgagroal_kill_connection(i, NULL);
            }
         }
         else
         {
            pgagroal_prometheus_connection_remove();
            pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i);
            pgagroal_kill_connection(i, NULL);
         }

         return true;
      }
   }

   return false;
}

static void
connection_details(int slot)
{
   int state;
   char time_buf[32];
   struct configuration* config;
   struct connection connection;

   config = (struct configuration*)shmem;

   connection = config->connections[slot];
   state = atomic_load(&config->states[slot]);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&(connection.timestamp), &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   switch (state)
   {
      case STATE_NOTINIT:
         pgagroal_log_debug("pgagroal_pool_status: State: NOTINIT");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         break;
      case STATE_INIT:
         pgagroal_log_debug("pgagroal_pool_status: State: INIT");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         break;
      case STATE_FREE:
         pgagroal_log_debug("pgagroal_pool_status: State: FREE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_IN_USE:
         pgagroal_log_debug("pgagroal_pool_status: State: IN_USE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_GRACEFULLY:
         pgagroal_log_debug("pgagroal_pool_status: State: GRACEFULLY");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_FLUSH:
         pgagroal_log_debug("pgagroal_pool_status: State: FLUSH");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_IDLE_CHECK:
         pgagroal_log_debug("pgagroal_pool_status: State: IDLE CHECK");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_VALIDATION:
         pgagroal_log_debug("pgagroal_pool_status: State: VALIDATION");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_REMOVE:
         pgagroal_log_debug("pgagroal_pool_status: State: REMOVE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      default:
         pgagroal_log_debug("pgagroal_pool_status: State %d Slot %d FD %d", state, slot, connection.fd);
         break;
   }
}

static bool
do_prefill(char* username, char* database, int size)
{
   signed char state;
   int free = 0;
   int connections = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->max_connections; i++)
   {
      if (!strcmp((const char*)(&config->connections[i].username), username) &&
          !strcmp((const char*)(&config->connections[i].database), database))
      {
         connections++;
      }
      else
      {
         state = atomic_load(&config->states[i]);

         if (state == STATE_NOTINIT)
         {
            free++;
         }
      }
   }

   return connections < size && free > 0;
}
