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

#define ZF_LOG_TAG "pool"
#include <zf_log.h>

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

static int find_best_rule(void* shmem, char* username, char* database);
static bool remove_connection(void* shmem, char* username, char* database);
static void connection_details(void* shmem, int slot);
static bool do_prefill(void* shmem, char* username, char* database, int size);

int
pgagroal_get_connection(void* shmem, char* username, char* database, bool reuse, bool transaction_mode, int* slot)
{
   bool prefill;
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
   struct configuration* config;

   config = (struct configuration*)shmem;

   prefill = false;

   pgagroal_prometheus_connection_get(shmem);

   best_rule = find_best_rule(shmem, username, database);
   retries = 0;
   start_time = time(NULL);

start:

   *slot = -1;
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
      if (do_init)
      {
         /* We need to find the server for the connection */
         if (pgagroal_get_primary(shmem, &server))
         {
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            if (!fork())
            {
               pgagroal_flush(shmem, FLUSH_GRACEFULLY);
            }

            goto error;
         }

         ZF_LOGD("connect: server %d", server);
         
         if (pgagroal_connect(shmem, config->servers[server].host, config->servers[server].port, &fd))
         {
            ZF_LOGE("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            pgagroal_prometheus_server_error(server, shmem);

            if (!fork())
            {
               pgagroal_flush(shmem, FLUSH_GRACEFULLY);
            }

            if (config->failover)
            {
               pgagroal_server_force_failover(config, server);
               pgagroal_prometheus_failed_servers(shmem);
               goto retry;
            }

            goto error;
         }

         ZF_LOGD("connect: %s:%d using slot %d fd %d", config->servers[server].host, config->servers[server].port, *slot, fd);
         
         config->connections[*slot].server = server;

         memset(&config->connections[*slot].username, 0, MAX_USERNAME_LENGTH);
         memcpy(&config->connections[*slot].username, username, MIN(strlen(username), MAX_USERNAME_LENGTH - 1));

         memset(&config->connections[*slot].database, 0, MAX_DATABASE_LENGTH);
         memcpy(&config->connections[*slot].database, database, MIN(strlen(database), MAX_DATABASE_LENGTH - 1));

         config->connections[*slot].limit_rule = best_rule;
         config->connections[*slot].has_security = SECURITY_INVALID;
         config->connections[*slot].timestamp = time(NULL);
         config->connections[*slot].pid = getpid();
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

            ZF_LOGD("pgagroal_get_connection: Slot %d FD %d - Error", *slot, config->connections[*slot].fd);
            pgagroal_tracking_event_slot(TRACKER_BAD_CONNECTION, *slot, shmem);
            status = pgagroal_kill_connection(shmem, *slot);
            prefill = true;
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

      if (prefill)
      {
         if (!fork())
         {
            pgagroal_prefill(shmem, false);
         }
      }

      config->connections[*slot].limit_rule = best_rule;
      config->connections[*slot].pid = getpid();
      config->connections[*slot].timestamp = time(NULL);

      pgagroal_prometheus_connection_success(shmem);
      pgagroal_tracking_event_slot(TRACKER_GET_CONNECTION_SUCCESS, *slot, shmem);

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
            remove_connection(shmem, username, database);
         }

         goto start;
      }
      else
      {
         if (!transaction_mode)
         {
            if (best_rule == -1)
            {
               if (remove_connection(shmem, username, database))
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

   pgagroal_prometheus_connection_timeout(shmem);
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_TIMEOUT, username, database, shmem);

   return 1;

error:
   if (best_rule >= 0)
   {
      atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
   }
   atomic_fetch_sub(&config->active_connections, 1);

   pgagroal_prometheus_connection_error(shmem);
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_ERROR, username, database, shmem);

   return 2;
}

int
pgagroal_return_connection(void* shmem, int slot, bool transaction_mode)
{
   int state;
   struct configuration* config;

   config = (struct configuration*)shmem;

   /* Verify the socket for the slot */
   if (!transaction_mode && !pgagroal_socket_isvalid(config->connections[slot].fd))
   {
      ZF_LOGD("pgagroal_return_connection: Slot %d FD %d - Error", slot, config->connections[slot].fd);
      config->connections[slot].has_security = SECURITY_INVALID;
   }

   /* Can we cache this connection ? */
   if (config->connections[slot].has_security != SECURITY_INVALID &&
       (config->connections[slot].has_security != SECURITY_SCRAM256 ||
        (config->connections[slot].has_security == SECURITY_SCRAM256 &&
         (config->authquery || pgagroal_user_known(config->connections[slot].username, shmem)))))
   {
      state = atomic_load(&config->states[slot]);

      /* Return the connection, if not GRACEFULLY */
      if (state == STATE_IN_USE)
      {
         ZF_LOGD("pgagroal_return_connection: Slot %d FD %d", slot, config->connections[slot].fd);

         if (!transaction_mode)
         {
            if (pgagroal_write_discard_all(NULL, config->connections[slot].fd))
            {
               goto kill_connection;
            }
         }

         pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_SUCCESS, slot, shmem);

         config->connections[slot].timestamp = time(NULL);

         if (config->connections[slot].new)
         {
            pgagroal_management_transfer_connection(shmem, slot);
         }

         pgagroal_management_return_connection(shmem, slot);

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

         pgagroal_prometheus_connection_return(shmem);

         return 0;
      }
      else if (state == STATE_GRACEFULLY)
      {
         pgagroal_write_terminate(NULL, config->connections[slot].fd);
      }
   }

kill_connection:

   pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_KILL, slot, shmem);

   return pgagroal_kill_connection(shmem, slot);
}

int
pgagroal_kill_connection(void* shmem, int slot)
{
   int result = 0;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_kill_connection: Slot %d FD %d State %d PID %d",
           slot, config->connections[slot].fd, atomic_load(&config->states[slot]),
           config->connections[slot].pid);

   pgagroal_tracking_event_slot(TRACKER_KILL_CONNECTION, slot, shmem);

   fd = config->connections[slot].fd;
   if (fd != -1)
   {
      pgagroal_management_kill_connection(shmem, slot, fd);
      pgagroal_disconnect(fd);
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

   config->connections[slot].limit_rule = -1;
   config->connections[slot].timestamp = -1;
   config->connections[slot].fd = -1;
   config->connections[slot].pid = -1;

   atomic_store(&config->states[slot], STATE_NOTINIT);

   pgagroal_prometheus_connection_kill(shmem);

   return result;
}

void
pgagroal_idle_timeout(void* shmem)
{
   bool prefill;
   time_t now;
   signed char free;
   signed char idle_check;
   struct configuration* config;

   pgagroal_start_logging(shmem);
   pgagroal_memory_init(shmem);

   config = (struct configuration*)shmem;
   now = time(NULL);
   prefill = false;

   ZF_LOGD("pgagroal_idle_timeout");

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
            pgagroal_prometheus_connection_idletimeout(shmem);
            pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i, shmem);
            pgagroal_kill_connection(shmem, i);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &idle_check, STATE_FREE))
            {
               pgagroal_prometheus_connection_idletimeout(shmem);
               pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i, shmem);
               pgagroal_kill_connection(shmem, i);
               prefill = true;
            }
         }
      }
   }
   
   if (prefill)
   {
      if (!fork())
      {
         pgagroal_prefill(shmem, false);
      }
   }

   pgagroal_pool_status(shmem);
   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(0);
}

void
pgagroal_validation(void* shmem)
{
   bool prefill = true;
   time_t now;
   signed char free;
   signed char validation;
   struct configuration* config;

   pgagroal_start_logging(shmem);
   pgagroal_memory_init(shmem);

   config = (struct configuration*)shmem;
   now = time(NULL);

   ZF_LOGD("pgagroal_validation");

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
            pgagroal_prometheus_connection_invalid(shmem);
            pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i, shmem);
            pgagroal_kill_connection(shmem, i);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &validation, STATE_FREE))
            {
               pgagroal_prometheus_connection_invalid(shmem);
               pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i, shmem);
               pgagroal_kill_connection(shmem, i);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      if (!fork())
      {
         pgagroal_prefill(shmem, false);
      }
   }

   pgagroal_pool_status(shmem);
   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(0);
}

void
pgagroal_flush(void* shmem, int mode)
{
   bool prefill;
   signed char free;
   signed char in_use;
   bool do_kill;
   signed char server_state;
   struct configuration* config;

   pgagroal_start_logging(shmem);
   pgagroal_memory_init(shmem);

   config = (struct configuration*)shmem;

   prefill = false;

   ZF_LOGD("pgagroal_flush");
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
         if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_FLUSH))
         {
            if (pgagroal_socket_isvalid(config->connections[i].fd))
            {
               pgagroal_write_terminate(NULL, config->connections[i].fd);
            }
            pgagroal_prometheus_connection_flush(shmem);
            pgagroal_tracking_event_slot(TRACKER_FLUSH, i, shmem);
            pgagroal_kill_connection(shmem, i);
            prefill = true;
         }
         else if (mode == FLUSH_ALL || mode == FLUSH_GRACEFULLY)
         {
            if (atomic_compare_exchange_strong(&config->states[i], &in_use, STATE_FLUSH))
            {
               if (mode == FLUSH_ALL)
               {
                  kill(config->connections[i].pid, SIGQUIT);
                  pgagroal_prometheus_connection_flush(shmem);
                  pgagroal_tracking_event_slot(TRACKER_FLUSH, i, shmem);
                  pgagroal_kill_connection(shmem, i);
                  prefill = true;
               }
               else if (mode == FLUSH_GRACEFULLY)
               {
                  atomic_store(&config->states[i], STATE_GRACEFULLY);
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
               pgagroal_prometheus_connection_flush(shmem);
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i, shmem);
               pgagroal_kill_connection(shmem, i);
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
   
   if (prefill)
   {
      if (!fork())
      {
         pgagroal_prefill(shmem, false);
      }
   }

   pgagroal_pool_status(shmem);
   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(0);
}

void
pgagroal_prefill(void* shmem, bool initial)
{
   struct configuration* config;

   pgagroal_start_logging(shmem);
   pgagroal_memory_init(shmem);

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_prefill");

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
               while (do_prefill(shmem, config->users[user].username, config->limits[i].database, size))
               {
                  int32_t slot = -1;

                  if (pgagroal_prefill_auth(config->users[user].username, config->users[user].password,
                                            config->limits[i].database, shmem, &slot) != AUTH_SUCCESS)
                  {
                     ZF_LOGW("Invalid data for user '%s' using limit entry (%d)", config->limits[i].username, i);

                     if (slot != -1)
                     {
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot, shmem);
                        pgagroal_kill_connection(shmem, slot);
                     }

                     break;
                  }

                  if (slot != -1)
                  {
                     if (config->connections[slot].has_security != SECURITY_INVALID)
                     {
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_RETURN, slot, shmem);
                        pgagroal_return_connection(shmem, slot, false);
                     }
                     else
                     {
                        ZF_LOGW("Unsupported security model during prefill for user '%s' using limit entry (%d)", config->limits[i].username, i);
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot, shmem);
                        pgagroal_kill_connection(shmem, slot);
                        break;
                     }
                  }
               }
            }
            else
            {
               ZF_LOGW("Unknown user '%s' for limit entry (%d)", config->limits[i].username, i);
            }
         }
         else
         {
            ZF_LOGW("Limit entry (%d) with invalid definition", i);
         }
      }
   }

   pgagroal_pool_status(shmem);
   pgagroal_memory_destroy();
   pgagroal_stop_logging(shmem);

   exit(0);
}

int
pgagroal_pool_init(void* shmem)
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
pgagroal_pool_shutdown(void* shmem)
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
pgagroal_pool_status(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_pool_status: %d/%d", atomic_load(&config->active_connections), config->max_connections);

   for (int i = 0; i < config->max_connections; i++)
   {
      connection_details(shmem, i);
   }

#ifdef DEBUG
   assert(atomic_load(&config->active_connections) <= config->max_connections);
#endif

   return 0;
}

static int
find_best_rule(void* shmem, char* username, char* database)
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
remove_connection(void* shmem, char* username, char* database)
{
   signed char free;
   signed char remove;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ZF_LOGV("remove_connection");
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
               pgagroal_prometheus_connection_remove(shmem);
               pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i, shmem);
               pgagroal_kill_connection(shmem, i);
            }
         }
         else
         {
            pgagroal_prometheus_connection_remove(shmem);
            pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i, shmem);
            pgagroal_kill_connection(shmem, i);
         }

         return true;
      }
   }

   return false;
}

static void
connection_details(void* shmem, int slot)
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
         ZF_LOGD("pgagroal_pool_status: State: NOTINIT");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      FD: %d", connection.fd);
         break;
      case STATE_INIT:
         ZF_LOGD("pgagroal_pool_status: State: INIT");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      FD: %d", connection.fd);
         break;
      case STATE_FREE:
         ZF_LOGD("pgagroal_pool_status: State: FREE");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      case STATE_IN_USE:
         ZF_LOGD("pgagroal_pool_status: State: IN_USE");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      case STATE_GRACEFULLY:
         ZF_LOGD("pgagroal_pool_status: State: GRACEFULLY");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      case STATE_FLUSH:
         ZF_LOGD("pgagroal_pool_status: State: FLUSH");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      case STATE_IDLE_CHECK:
         ZF_LOGD("pgagroal_pool_status: State: IDLE CHECK");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      case STATE_VALIDATION:
         ZF_LOGD("pgagroal_pool_status: State: VALIDATION");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      case STATE_REMOVE:
         ZF_LOGD("pgagroal_pool_status: State: REMOVE");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      AppName: %s", connection.appname);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", &time_buf[0]);
         ZF_LOGD("                      FD: %d", connection.fd);
         ZF_LOGV("                      PID: %d", connection.pid);
         ZF_LOGV("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            ZF_LOGV("                      Size: %zd", connection.security_lengths[i]);
            ZF_LOGV_MEM(&connection.security_messages[i], connection.security_lengths[i],
                        "                      Message %p:", (const void *)&connection.security_messages[i]);
         }
         break;
      default:
         ZF_LOGD("pgagroal_pool_status: State %d Slot %d FD %d", state, slot, connection.fd);
         break;
   }
}

static bool
do_prefill(void* shmem, char* username, char* database, int size)
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
