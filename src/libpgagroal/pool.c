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
#include <network.h>
#include <management.h>
#include <message.h>
#include <pool.h>
#include <server.h>

#define ZF_LOG_TAG "pool"
#include <zf_log.h>

/* system */
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void connection_details(void* shmem, int slot);

int
pgagroal_get_connection(void* shmem, char* username, char* database, int* slot)
{
   bool create_connection = true;
   bool do_init = false;
   int connections;
   int not_init;
   int free;
   int server;
   int fd;
   size_t max;
   struct configuration* config;

   config = (struct configuration*)shmem;
   *slot = -1;

   /* Try and find an existing free connection */
   for (int i = 0; *slot == -1 && i < NUMBER_OF_CONNECTIONS; i++)
   {
      free = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->connections[i].state, &free, STATE_IN_USE))
      {
         if (!strcmp((const char*)(&config->connections[i].username), username) &&
             !strcmp((const char*)(&config->connections[i].database), database))
         {
            *slot = i;
         }
         else
         {
            atomic_store(&config->connections[i].state, STATE_FREE);
         }
      }
   }

   if (*slot == -1)
   {
      if (config->max_connections > 0)
      {
         connections = atomic_fetch_add(&config->number_of_connections, 1);
         if (connections > config->max_connections)
         {
            create_connection = false;
            atomic_fetch_sub(&config->number_of_connections, 1);
         }
      }

      if (create_connection)
      {
         /* Ok, try and create a new connection */
         for (int i = 0; *slot == -1 && i < NUMBER_OF_CONNECTIONS; i++)
         {
            not_init = STATE_NOTINIT;

            if (atomic_compare_exchange_strong(&config->connections[i].state, &not_init, STATE_INIT))
            {
               *slot = i;
               do_init = true;
            }
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

         config->connections[*slot].has_security = -1;
         config->connections[*slot].timestamp = time(NULL);
         config->connections[*slot].fd = fd;

         atomic_store(&config->connections[*slot].state, STATE_IN_USE);
      }
      else
      {
         bool kill = false;
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

            return pgagroal_get_connection(shmem, username, database, slot);
         }
      }

      return 0;
   }
   else
   {
      /* Try and free connections, and recurse */
   }

error:

   return 1;
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
      config->connections[slot].has_security = -1;
   }

   /* We can't cache SCRAM-SHA-256 connections atm */
   if (config->connections[slot].has_security != -1 && config->connections[slot].has_security != 10)
   {
      state = atomic_load(&config->connections[slot].state);

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

         config->connections[slot].new = false;
         atomic_store(&config->connections[slot].state, STATE_FREE);
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

   ZF_LOGD("pgagroal_kill_connection: Slot %d FD %d", slot, config->connections[slot].fd);

   fd = config->connections[slot].fd;
   pgagroal_management_kill_connection(shmem, slot);
   pgagroal_disconnect(fd);
   config->connections[slot].fd = -1;

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      config->connections[slot].security_lengths[i] = 0;
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   config->connections[slot].new = true;
   atomic_store(&config->connections[slot].state, STATE_NOTINIT);

   if (config->max_connections > 0)
   {
      atomic_fetch_sub(&config->number_of_connections, 1);
   }
   
   return 0;
}

void
pgagroal_idle_timeout(void* shmem)
{
   time_t now;
   int free;
   struct configuration* config;

   pgagroal_start_logging(shmem);

   config = (struct configuration*)shmem;
   now = time(NULL);

   ZF_LOGD("pgagroal_idle_timeout");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = NUMBER_OF_CONNECTIONS - 1; i >= 0; i--)
   {
      free = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->connections[i].state, &free, STATE_IDLE_CHECK))
      {
         double diff = difftime(now, config->connections[i].timestamp);
         if (diff >= (double)config->idle_timeout)
         {
            pgagroal_kill_connection(shmem, i);
         }
         else
         {
            atomic_store(&config->connections[i].state, STATE_FREE);
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
   int free;
   struct configuration* config;

   pgagroal_start_logging(shmem);

   config = (struct configuration*)shmem;
   now = time(NULL);

   ZF_LOGD("pgagroal_validation");

   /* We run backwards */
   for (int i = NUMBER_OF_CONNECTIONS - 1; i >= 0; i--)
   {
      free = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->connections[i].state, &free, STATE_VALIDATION))
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
            atomic_store(&config->connections[i].state, STATE_FREE);
         }
      }
   }

   pgagroal_pool_status(shmem);
   pgagroal_stop_logging(shmem);

   exit(0);
}

int
pgagroal_flush(void* shmem, int mode)
{
   int free;
   int in_use;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ZF_LOGD("pgagroal_flush");
   for (int i = NUMBER_OF_CONNECTIONS - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      in_use = STATE_IN_USE;

      if (atomic_compare_exchange_strong(&config->connections[i].state, &free, STATE_FLUSH))
      {
         pgagroal_kill_connection(shmem, i);
      }
      else if (mode == FLUSH_ALL || mode == FLUSH_GRACEFULLY)
      {
         if (atomic_compare_exchange_strong(&config->connections[i].state, &in_use, STATE_FLUSH))
         {
            if (mode == FLUSH_ALL)
            {
               pgagroal_kill_connection(shmem, i);
            }
            else if (mode == FLUSH_GRACEFULLY)
            {
               atomic_store(&config->connections[i].state, STATE_GRACEFULLY);
            }
         }
      }
   }
   
   pgagroal_pool_status(shmem);
   return 0;
}

int
pgagroal_pool_init(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_CONNECTIONS; i++)
   {
      atomic_init(&config->connections[i].state, STATE_NOTINIT);
      config->connections[i].new = true;
      config->connections[i].fd = -1;
   }

   return 0;
}

int
pgagroal_pool_shutdown(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_CONNECTIONS; i++)
   {
      int state = atomic_load(&config->connections[i].state);

      if (state != STATE_NOTINIT)
      {
         pgagroal_disconnect(config->connections[i].fd);
         atomic_store(&config->connections[i].state, STATE_NOTINIT);
      }
   }

   return 0;
}

int
pgagroal_pool_status(void* shmem)
{
   for (int i = 0; i < NUMBER_OF_CONNECTIONS; i++)
   {
      connection_details(shmem, i);
   }

   return 0;
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
   state = atomic_load(&connection.state);
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
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
         ZF_LOGD("                      Time: %s", time);
         ZF_LOGV("                      FD: %d", connection.fd);
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
