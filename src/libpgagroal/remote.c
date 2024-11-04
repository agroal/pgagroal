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
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <remote.h>
#include <security.h>
#include <utils.h>

/* system */
#include <ev.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

void
pgagroal_remote_management(int client_fd, char* address)
{
   int server_fd = -1;
   int exit_code;
   int auth_status;
   uint8_t compression;
   uint8_t encryption;
   SSL* client_ssl = NULL;
   struct json* payload = NULL;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   exit_code = 0;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_remote_management: connect %d", client_fd);

   auth_status = pgagroal_remote_management_auth(client_fd, address, &client_ssl);
   if (auth_status == AUTH_SUCCESS)
   {
      if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &server_fd))
      {
         goto done;
      }

      if (pgagroal_management_read_json(client_ssl, client_fd, &compression, &encryption, &payload))
      {
         goto done;
      }

      if (pgagroal_management_write_json(NULL, server_fd, compression, encryption, payload))
      {
         goto done;
      }

      pgagroal_json_destroy(payload);
      payload = NULL;

      if (pgagroal_management_read_json(NULL, server_fd, &compression, &encryption, &payload))
      {
         goto done;
      }

      if (pgagroal_management_write_json(client_ssl, client_fd, compression, encryption, payload))
      {
         goto done;
      }
   }
   else
   {
      exit_code = 1;
   }

done:

   pgagroal_json_destroy(payload);
   payload = NULL;

   if (client_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(client_ssl);
      res = SSL_shutdown(client_ssl);
      if (res == 0)
      {
         SSL_shutdown(client_ssl);
      }
      SSL_free(client_ssl);
      SSL_CTX_free(ctx);
   }

   pgagroal_log_debug("pgagroal_remote_management: disconnect %d", client_fd);
   pgagroal_disconnect(client_fd);
   pgagroal_disconnect(server_fd);

   free(address);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(exit_code);
}
