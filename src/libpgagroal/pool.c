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
#include <message.h>
#include <pool.h>
#include <server.h>

#define ZF_LOG_TAG "pool"
#include <zf_log.h>

/* system */
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

static int find_best_rule(void* shmem, char* username, char* database);
static bool remove_connection(void* shmem, char* username, char* database);
static void connection_details(void* shmem, int slot);

int
pgagroal_get_connection(void* shmem, char* username, char* database, int* slot)
{
   bool do_init;
   bool has_lock;
   int connections;
   signed char not_init;
   signed char free;
   int server;
   int fd;
   size_t max;
   time_t start_time;
   int best_rule;
   int retries;
   struct configuration* config;

   config = (struct configuration*)shmem;
   *slot = -1;
   best_rule = find_best_rule(shmem, username, database);
   retries = 0;

   start_time = time(NULL);

start:

   do_init = false;
   has_lock = false;

   if (best_rule >= 0)
   {
      connections = atomic_fetch_add(&config->limits[best_rule].active_connections, 1);
      if (connections >= config->limits[best_rule].max_connections)
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

   if (*slot == -1)
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
         pgagroal_get_primary(shmem, &server);

         ZF_LOGD("connect: server %d", server);
         
         if (pgagroal_connect(shmem, config->servers[server].host, config->servers[server].port, &fd))
         {
            ZF_LOGE("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
            goto error;
         }

         ZF_LOGD("connect: %s:%d using slot %d fd %d", config->servers[server].host, config->servers[server].port, *slot, fd);
         
         config->connections[*slot].server = server;

         max = strlen(username);
         if (max > IDENTIFIER_LENGTH - 1)
            max = IDENTIFIER_LENGTH - 1;
         memcpy(&config->connections[*slot].username, username, max);

         max = strlen(database);
         if (max > IDENTIFIER_LENGTH - 1)
            max = IDENTIFIER_LENGTH - 1;
         memcpy(&config->connections[*slot].database, database, max);

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

         config->connections[*slot].limit_rule = best_rule;
         config->connections[*slot].pid = getpid();
         config->connections[*slot].timestamp = time(NULL);

         /* Verify the socket for the slot */
         int r = fcntl(config->connections[*slot].fd, F_GETFL);
         if (r == -1)
         {
            kill = true;
         }

         if (config->validation == VALIDATION_FOREGROUND)
         {
            kill = !pgagroal_connection_isvalid(config->connections[*slot].fd);
         }

         if (kill)
         {
            ZF_LOGD("pgagroal_get_connection: Slot %d FD %d - Error", *slot, config->connections[*slot].fd);
            pgagroal_kill_connection(shmem, *slot);

            goto retry2;
         }
      }

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

         remove_connection(shmem, username, database);
         goto start;
      }
      else
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
   }

timeout:
   return 1;

error:
   if (best_rule >= 0)
   {
      atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
   }
   atomic_fetch_sub(&config->active_connections, 1);

   return 2;
}

int
pgagroal_return_connection(void* shmem, int slot)
{
   int r;
   int state;
   struct configuration* config;

   config = (struct configuration*)shmem;

   /* Verify the socket for the slot */
   r = fcntl(config->connections[slot].fd, F_GETFL);
   if (r == -1)
   {
      ZF_LOGD("pgagroal_return_connection: Slot %d FD %d - Error", slot, config->connections[slot].fd);
      config->connections[slot].has_security = SECURITY_INVALID;
   }

   /* We can't cache SCRAM-SHA-256 connections atm */
   if (config->connections[slot].has_security != SECURITY_INVALID && config->connections[slot].has_security != SECURITY_SCRAM256)
   {
      state = atomic_load(&config->states[slot]);

      /* Return the connection, if not GRACEFULLY */
      if (state == STATE_IN_USE)
      {
         ZF_LOGD("pgagroal_return_connection: Slot %d FD %d", slot, config->connections[slot].fd);

         pgagroal_write_deallocate_all(config->connections[slot].fd);

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
         atomic_store(&config->states[slot], STATE_FREE);
         atomic_fetch_sub(&config->active_connections, 1);

         return 0;
      }
   }

   return pgagroal_kill_connection(shmem, slot);
}

int
pgagroal_kill_connection(void* shmem, int slot)
{
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_kill_connection: Slot %d FD %d State %d PID %d",
           slot, config->connections[slot].fd, atomic_load(&config->states[slot]),
           config->connections[slot].pid);

   fd = config->connections[slot].fd;
   pgagroal_management_kill_connection(shmem, slot);
   pgagroal_disconnect(fd);

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

   config->connections[slot].new = true;
   config->connections[slot].server = 0;

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

   return 0;
}

void
pgagroal_idle_timeout(void* shmem)
{
   time_t now;
   signed char free;
   struct configuration* config;

   pgagroal_start_logging(shmem);

   config = (struct configuration*)shmem;
   now = time(NULL);

   ZF_LOGD("pgagroal_idle_timeout");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_IDLE_CHECK))
      {
         double diff = difftime(now, config->connections[i].timestamp);
         if (diff >= (double)config->idle_timeout)
         {
            pgagroal_kill_connection(shmem, i);
         }
         else
         {
            atomic_store(&config->states[i], STATE_FREE);
         }
      }
   }
   
   pgagroal_pool_status(shmem);
   pgagroal_stop_logging(shmem);

   exit(0);
}

void
pgagroal_validation(void* shmem)
{
   time_t now;
   signed char free;
   struct configuration* config;

   pgagroal_start_logging(shmem);

   config = (struct configuration*)shmem;
   now = time(NULL);

   ZF_LOGD("pgagroal_validation");

   /* We run backwards */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_VALIDATION))
      {
         bool kill = false;
         double diff;

         /* Verify the socket for the slot */
         int r = fcntl(config->connections[i].fd, F_GETFL);
         if (r == -1)
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
            pgagroal_kill_connection(shmem, i);
         }
         else
         {
            atomic_store(&config->states[i], STATE_FREE);
         }
      }
   }

   pgagroal_pool_status(shmem);
   pgagroal_stop_logging(shmem);

   exit(0);
}

void
pgagroal_flush(void* shmem, int mode)
{
   signed char free;
   signed char in_use;
   struct configuration* config;

   pgagroal_start_logging(shmem);

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_flush");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      in_use = STATE_IN_USE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_FLUSH))
      {
         pgagroal_kill_connection(shmem, i);
      }
      else if (mode == FLUSH_ALL || mode == FLUSH_GRACEFULLY)
      {
         if (atomic_compare_exchange_strong(&config->states[i], &in_use, STATE_FLUSH))
         {
            if (mode == FLUSH_ALL)
            {
               kill(config->connections[i].pid, SIGQUIT);
               pgagroal_kill_connection(shmem, i);
            }
            else if (mode == FLUSH_GRACEFULLY)
            {
               atomic_store(&config->states[i], STATE_GRACEFULLY);
            }
         }
      }
   }
   
   pgagroal_pool_status(shmem);
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
   struct configuration* config;

   config = (struct configuration*)shmem;

   ZF_LOGD("remove_connection");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_REMOVE))
      {
         if (!strcmp(username, config->connections[i].username) && !strcmp(database, config->connections[i].database))
         {
            atomic_store(&config->states[i], STATE_FREE);
         }
         else
         {
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
   char* time = NULL;
   struct configuration* config;
   struct connection connection;

   config = (struct configuration*)shmem;

   connection = config->connections[slot];
   state = atomic_load(&config->states[slot]);
   time = ctime(&(connection.timestamp));

   switch (state)
   {
      case STATE_NOTINIT:
         ZF_LOGD("pgagroal_pool_status: State: NOTINIT");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGV("                      FD: %d", connection.fd);
         break;
      case STATE_INIT:
         ZF_LOGD("pgagroal_pool_status: State: INIT");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGV("                      FD: %d", connection.fd);
         break;
      case STATE_FREE:
         ZF_LOGD("pgagroal_pool_status: State: FREE");
         ZF_LOGD("                      Slot: %d", slot);
         ZF_LOGD("                      Server: %d", connection.server);
         ZF_LOGD("                      User: %s", connection.username);
         ZF_LOGD("                      Database: %s", connection.database);
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Rule: %d", connection.limit_rule);
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
