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
#include <message.h>
#include <pool.h>
#include <server.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int failover(int old_primary);

int
pgagroal_get_primary(int* server)
{
   int primary;
   signed char server_state;
   struct main_configuration* config;

   primary = -1;
   config = (struct main_configuration*)shmem;

   /* Find PRIMARY */
   for (int i = 0; primary == -1 && i < config->number_of_servers; i++)
   {
      server_state = atomic_load(&config->servers[i].state);
      if (server_state == SERVER_PRIMARY)
      {
         pgagroal_log_trace("pgagroal_get_primary: server (%d) name (%s) primary", i, config->servers[i].name);
         primary = i;
      }
   }

   /* Find NOTINIT_PRIMARY */
   for (int i = 0; primary == -1 && i < config->number_of_servers; i++)
   {
      server_state = atomic_load(&config->servers[i].state);
      if (server_state == SERVER_NOTINIT_PRIMARY)
      {
         pgagroal_log_trace("pgagroal_get_primary: server (%d) name (%s) noninit_primary", i, config->servers[i].name);
         primary = i;
      }
   }

   /* Find the first valid server */
   for (int i = 0; primary == -1 && i < config->number_of_servers; i++)
   {
      server_state = atomic_load(&config->servers[i].state);
      if (server_state != SERVER_FAILOVER && server_state != SERVER_FAILED)
      {
         pgagroal_log_trace("pgagroal_get_primary: server (%d) name (%s) any (%d)", i, config->servers[i].name, server_state);
         primary = i;
      }
   }

   if (primary == -1)
   {
      goto error;
   }

   *server = primary;

   return 0;

error:

   *server = -1;

   return 1;
}

int
pgagroal_update_server_state(int slot, int socket, SSL* ssl)
{
   int status;
   int server;
   size_t size = 40;
   signed char state;
   char is_recovery[size];
   struct message qmsg;
   struct message* tmsg = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   server = config->connections[slot].server;

   memset(&qmsg, 0, sizeof(struct message));
   memset(&is_recovery, 0, size);

   pgagroal_write_byte(&is_recovery, 'Q');
   pgagroal_write_int32(&(is_recovery[1]), size - 1);
   pgagroal_write_string(&(is_recovery[5]), "SELECT * FROM pg_is_in_recovery();");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = &is_recovery;

   status = pgagroal_write_message(ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(ssl, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   /* Read directly from the D message fragment */
   state = pgagroal_read_byte(tmsg->data + 54);

   pgagroal_free_message(tmsg);

   if (state == 'f')
   {
      atomic_store(&config->servers[server].state, SERVER_PRIMARY);
   }
   else
   {
      atomic_store(&config->servers[server].state, SERVER_REPLICA);
   }

   pgagroal_free_message(tmsg);

   return 0;

error:
   pgagroal_log_trace("pgagroal_update_server_state: slot (%d) status (%d)", slot, status);

   pgagroal_free_message(tmsg);

   return 1;
}

int
pgagroal_server_status(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      if (strlen(config->servers[i].name) > 0)
      {
         pgagroal_log_debug("pgagroal_server_status:    #: %d", i);
         pgagroal_log_debug("                        Name: %s", config->servers[i].name);
         pgagroal_log_debug("                        Host: %s", config->servers[i].host);
         pgagroal_log_debug("                        Port: %d", config->servers[i].port);
         switch (atomic_load(&config->servers[i].state))
         {
            case SERVER_NOTINIT:
               pgagroal_log_debug("                        State: NOTINIT");
               break;
            case SERVER_NOTINIT_PRIMARY:
               pgagroal_log_debug("                        State: NOTINIT_PRIMARY");
               break;
            case SERVER_PRIMARY:
               pgagroal_log_debug("                        State: PRIMARY");
               break;
            case SERVER_REPLICA:
               pgagroal_log_debug("                        State: REPLICA");
               break;
            case SERVER_FAILOVER:
               pgagroal_log_debug("                        State: FAILOVER");
               break;
            case SERVER_FAILED:
               pgagroal_log_debug("                        State: FAILED");
               break;
            default:
               pgagroal_log_debug("                        State: %d", atomic_load(&config->servers[i].state));
               break;
         }
      }
   }

   return 0;
}

int
pgagroal_server_failover(int slot)
{
   signed char primary;
   signed char old_primary;
   int ret = 1;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   primary = SERVER_PRIMARY;

   old_primary = config->connections[slot].server;

   if (atomic_compare_exchange_strong(&config->servers[old_primary].state, &primary, SERVER_FAILOVER))
   {
      ret = failover(old_primary);

      if (!fork())
      {
         pgagroal_flush_server(old_primary);
      }
   }

   return ret;
}

int
pgagroal_server_force_failover(int server)
{
   signed char cur_state;
   signed char prev_state;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   cur_state = atomic_load(&config->servers[server].state);

   if (cur_state != SERVER_FAILOVER && cur_state != SERVER_FAILED)
   {
      prev_state = atomic_exchange(&config->servers[server].state, SERVER_FAILOVER);

      if (prev_state == SERVER_NOTINIT || prev_state == SERVER_NOTINIT_PRIMARY || prev_state == SERVER_PRIMARY || prev_state == SERVER_REPLICA)
      {
         return failover(server);
      }
      else if (prev_state == SERVER_FAILED)
      {
         atomic_store(&config->servers[server].state, SERVER_FAILED);
      }
   }

   return 1;
}

int
pgagroal_server_clear(char* server)
{
   signed char state;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (!strcmp(config->servers[i].name, server))
      {
         state = atomic_load(&config->servers[i].state);

         if (state == SERVER_FAILED)
         {
            atomic_store(&config->servers[i].state, SERVER_NOTINIT);
         }

         return 0;
      }
   }

   return 1;
}

int
pgagroal_server_switch(char* server)
{
   int old_primary;
   int new_primary;
   signed char state;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   old_primary = -1;
   new_primary = -1;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      state = atomic_load(&config->servers[i].state);

      if (state == SERVER_PRIMARY)
      {
         old_primary = i;
      }
      else if (!strcmp(config->servers[i].name, server))
      {
         new_primary = i;
      }
   }

   if (old_primary != -1 && new_primary != -1)
   {
      atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
      atomic_store(&config->servers[new_primary].state, SERVER_PRIMARY);
      return 0;
   }
   else if (old_primary == -1 && new_primary != -1)
   {
      atomic_store(&config->servers[new_primary].state, SERVER_PRIMARY);
      return 0;
   }

   return 1;
}

static int
failover(int old_primary)
{
   signed char state;
   char old_primary_port[6];
   int new_primary;
   char new_primary_port[6];
   int status;
   pid_t pid;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   new_primary = -1;

   for (int i = 0; new_primary == -1 && i < config->number_of_servers; i++)
   {
      state = atomic_load(&config->servers[i].state);
      if (state == SERVER_NOTINIT || state == SERVER_NOTINIT_PRIMARY || state == SERVER_REPLICA)
      {
         new_primary = i;
      }
   }

   if (new_primary == -1)
   {
      pgagroal_log_error("Failover: New primary could not be found");
      atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
      goto error;
   }

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("Failover: Unable to execute failover script");
      atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
      goto error;
   }
   else if (pid > 0)
   {
      waitpid(pid, &status, 0);

      if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
      {
         pgagroal_log_info("Failover: New primary is %s (%s:%d)", config->servers[new_primary].name, config->servers[new_primary].host, config->servers[new_primary].port);
         atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
         atomic_store(&config->servers[new_primary].state, SERVER_PRIMARY);
      }
      else
      {
         if (WIFEXITED(status))
         {
            pgagroal_log_error("Failover: Error from failover script (exit %d)", WEXITSTATUS(status));
         }
         else
         {
            pgagroal_log_error("Failover: Error from failover script (status %d)", status);
         }

         atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
         atomic_store(&config->servers[new_primary].state, SERVER_FAILED);
      }
   }
   else
   {
      memset(&old_primary_port, 0, sizeof(old_primary_port));
      memset(&new_primary_port, 0, sizeof(new_primary_port));

      sprintf(&old_primary_port[0], "%d", config->servers[old_primary].port);
      sprintf(&new_primary_port[0], "%d", config->servers[new_primary].port);

      execl(config->failover_script, "pgagroal_failover",
            config->servers[old_primary].host, old_primary_port,
            config->servers[new_primary].host, new_primary_port,
            (char*)NULL);
   }

   return 0;

error:

   return 1;
}
