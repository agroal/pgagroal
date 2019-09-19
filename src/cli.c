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
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define ACTION_UNKNOWN 0
#define ACTION_FLUSH   1
#define ACTION_STOP    2

static void
version()
{
   printf("pgagroal-cli %s\n", VERSION);
   exit(1);
}

static void
usage()
{
   printf("pgagroal-cli %s\n", VERSION);
   printf("  Command line utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  flush-idle               Flush idle connections\n");
   printf("  flush-gracefully         Flush all connections gracefully\n");
   printf("  flush-all                Flush all connections. USE WITH CAUTION !\n");
   printf("  stop                     Stop pgagroal\n");
   printf("\n");
}

int
main(int argc, char **argv)
{
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   int c;
   int option_index = 0;
   void* shmem = NULL;
   size_t size;
   int32_t action = ACTION_UNKNOWN;
   int32_t mode = FLUSH_IDLE;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config",  required_argument, 0, 'c'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "V?c:",
                      long_options, &option_index);

      if (c == -1)
         break;

      switch (c)
      {
         case 'c':
            configuration_path = optarg;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(1);
            break;
         default:
            break;
      }
   }

   size = sizeof(struct configuration);
   shmem = pgagroal_create_shared_memory(size);
   pgagroal_init_configuration(shmem, size);
   
   if (configuration_path != NULL)
   {
      ret = pgagroal_read_configuration(configuration_path, shmem);
      if (ret)
      {
         printf("pgagroal-cli: Configuration not found: %s\n", configuration_path);
         exit(1);
      }
   }
   else
   {
      ret = pgagroal_read_configuration("/etc/pgagroal.conf", shmem);
      if (ret)
      {
         printf("pgagroal-cli: Configuration not found: /etc/pgagroal.conf\n");
         exit(1);
      }
   }

   pgagroal_start_logging(shmem);

   if (argc > 0)
   {
      if (!strcmp("flush-idle", argv[argc - 1]))
      {
         mode = FLUSH_IDLE;
         action = ACTION_FLUSH;
      }
      else if (!strcmp("flush-gracefully", argv[argc - 1]))
      {
         mode = FLUSH_GRACEFULLY;
         action = ACTION_FLUSH;
      }
      else if (!strcmp("flush-all", argv[argc - 1]))
      {
         mode = FLUSH_ALL;
         action = ACTION_FLUSH;
      }
      else if (!strcmp("stop", argv[argc - 1]))
      {
         action = ACTION_STOP;
      }

      if (action == ACTION_FLUSH)
      {
         pgagroal_management_flush(shmem, mode);
      }
      else if (action == ACTION_STOP)
      {
         pgagroal_management_stop(shmem);
      }
   }

   if (action == ACTION_UNKNOWN)
   {
      usage();
      exit_code = 1;
   }
   
   pgagroal_stop_logging(shmem);

   munmap(shmem, size);

   return exit_code;
}
