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
#include <configuration.h>
#include <logging.h>
#include <management.h>
#include <shmem.h>

#define ZF_LOG_TAG "cli"
#include <zf_log.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int
main(int argc, char **argv)
{
   int ret;
   void* shmem = NULL;
   size_t size;
   int32_t mode = FLUSH_IDLE;

   size = sizeof(struct configuration);
   shmem = pgagroal_create_shared_memory(size);
   pgagroal_init_configuration(shmem, size);
   
   ret = pgagroal_read_configuration("pgagroal.conf", shmem);
   if (ret)
      ret = pgagroal_read_configuration("/etc/pgagroal.conf", shmem);
   if (ret)
   {
      printf("pgagroal: Configuration not found\n");
      exit(1);
   }

   pgagroal_start_logging(shmem);

   if (argc > 1)
   {
      if (!strcmp("flush-idle", argv[1]))
      {
         mode = FLUSH_IDLE;
      }
      else if (!strcmp("flush-gracefully", argv[1]))
      {
         mode = FLUSH_GRACEFULLY;
      }
      if (!strcmp("flush-all", argv[1]))
      {
         mode = FLUSH_ALL;
      }

      pgagroal_management_flush(shmem, mode);
   }
   else
   {
      printf("Usage: %s [flush-idle|flush-gracefully|flush-all]\n", argv[0]);
   }
   
   pgagroal_stop_logging(shmem);

   munmap(shmem, size);

   return 0;
}
