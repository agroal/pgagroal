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
#include <message.h>
#include <server.h>
#include <utils.h>

#define ZF_LOG_TAG "server"
#include <zf_log.h>

/* system */
#include <stdlib.h>
#include <string.h>

int
pgagroal_get_primary(void* shmem, int* server)
{
   int primary;
   struct configuration* config;

   primary = -3;
   config = (struct configuration*)shmem;

   for (int i = 0; primary == -3 && i < NUMBER_OF_SERVERS; i++)
   {
      if (strlen(config->servers[i].name) > 0)
      {
         ZF_LOGV("pgagroal_get_primary: server (%d) name (%s) primary (%d)", i, config->servers[i].name, config->servers[i].primary);
         if (config->servers[i].primary == SERVER_PRIMARY ||
             config->servers[i].primary == SERVER_NOTINIT_PRIMARY)
         {
            primary = i;
         }
      }
   }

   /* Assume that the first server defined is primary */
   if (primary == -3)
      primary = 0;
   
   *server = primary;

   return 0;
}

int
pgagroal_update_server_state(void* shmem, int slot, int socket)
{
   int status;
   int server;
   size_t size = 40;
   signed char state;
   char is_recovery[size];
   struct message qmsg;
   struct message* tmsg = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;
   server = config->connections[slot].server;

   memset(&qmsg, 0, sizeof(struct message));
   memset(&is_recovery, 0, size);

   pgagroal_write_byte(&is_recovery, 'Q');
   pgagroal_write_int32(&(is_recovery[1]), size - 1);
   pgagroal_write_string(&(is_recovery[5]), "SELECT * FROM pg_is_in_recovery();");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = &is_recovery;

   status = pgagroal_write_message(socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   /* Read directly from the D message fragment */
   state = pgagroal_read_byte(tmsg->data + 54);
   
   pgagroal_free_message(tmsg);

   if (state == 'f')
   {
      config->servers[server].primary = SERVER_PRIMARY;
   }
   else
   {
      config->servers[server].primary = SERVER_REPLICA;
   }
   
   return 0;

error:
   ZF_LOGV("pgagroal_update_server_state: slot (%d) status (%d)", slot, status);

   if (tmsg)
      pgagroal_free_message(tmsg);

   return 1;
}

int
pgagroal_server_status(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      if (strlen(config->servers[i].name) > 0)
      {
         ZF_LOGD("pgagroal_server_status:    #: %d", i);
         ZF_LOGD("                        Name: %s", config->servers[i].name);
         ZF_LOGD("                        Host: %s", config->servers[i].host);
         ZF_LOGD("                        Port: %d", config->servers[i].port);
         switch (config->servers[i].primary)
         {
            case SERVER_NOTINIT:
               ZF_LOGD("                        Type: NOTINIT");
               break;
            case SERVER_NOTINIT_PRIMARY:
               ZF_LOGD("                        Type: NOTINIT_PRIMARY");
               break;
            case SERVER_PRIMARY:
               ZF_LOGD("                        Type: PRIMARY");
               break;
            case SERVER_REPLICA:
               ZF_LOGD("                        Type: REPLICA");
               break;
            default:
               ZF_LOGD("                        Type: %d", config->servers[i].primary);
               break;
         }
      }
   }

   return 0;
}
