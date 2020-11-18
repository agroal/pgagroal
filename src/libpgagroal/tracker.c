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
#include <server.h>
#include <tracker.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

static int count_connections(void);

void
pgagroal_tracking_event_basic(int id, char* username, char* database)
{
   int primary;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->tracker)
   {
      struct timeval t;
      long long milliseconds;

      gettimeofday(&t, NULL);
      milliseconds = t.tv_sec * 1000 + t.tv_usec / 1000;

      if (username == NULL)
      {
         username = "";
      }

      if (database == NULL)
      {
         database = "";
      }

      pgagroal_get_primary(&primary);

      pgagroal_log_info("PGAGROAL|%d|%d|%d|%lld|%d|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|",
                        id,
                        -1,
                        -3,
                        milliseconds,
                        getpid(),
                        username,
                        database,
                        "",
                        -1,
                        primary,
                        -1,
                        -3,
                        -1,
                        -1,
                        atomic_load(&config->active_connections),
                        count_connections());
   }
}

void
pgagroal_tracking_event_slot(int id, int slot)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->tracker)
   {
      char* username = NULL;
      char* database = NULL;
      char* appname = NULL;
      struct timeval t;
      long long milliseconds;

      gettimeofday(&t, NULL);
      milliseconds = t.tv_sec * 1000 + t.tv_usec / 1000;

      if (slot != -1)
      {
         username = &config->connections[slot].username[0];
         database = &config->connections[slot].database[0];
         appname = &config->connections[slot].appname[0];
      }
      else
      {
         username = "";
         database = "";
         appname = "";
      }
      
      pgagroal_log_info("PGAGROAL|%d|%d|%d|%lld|%d|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|",
                        id,
                        slot,
                        atomic_load(&config->states[slot]),
                        milliseconds,
                        getpid(),
                        username,
                        database,
                        appname,
                        config->connections[slot].new,
                        config->connections[slot].server,
                        config->connections[slot].tx_mode,
                        config->connections[slot].has_security,
                        config->connections[slot].limit_rule,
                        config->connections[slot].fd,
                        atomic_load(&config->active_connections),
                        count_connections());
   }
}

static int
count_connections(void)
{
   int active = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);
      switch (state)
      {
         case STATE_IN_USE:
         case STATE_GRACEFULLY:
            active++;
         default:
            break;
      }
   }

   return active;
}
