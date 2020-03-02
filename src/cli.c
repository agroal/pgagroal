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
#include <configuration.h>
#include <logging.h>
#include <management.h>
#include <network.h>
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

#define ACTION_UNKNOWN        0
#define ACTION_FLUSH          1
#define ACTION_GRACEFULLY     2
#define ACTION_STOP           3
#define ACTION_STATUS         4
#define ACTION_DETAILS        5
#define ACTION_ISALIVE        6
#define ACTION_CANCELSHUTDOWN 7
#define ACTION_ENABLEDB       8
#define ACTION_DISABLEDB      9

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
   printf("  is-alive                 Is pgagroal alive\n");
   printf("  enable                   Enable a database\n");
   printf("  disable                  Disable a database\n");
   printf("  gracefully               Stop pgagroal gracefully\n");
   printf("  stop                     Stop pgagroal\n");
   printf("  cancel-shutdown          Cancel the graceful shutdown\n");
   printf("  status                   Status of pgagroal\n");
   printf("  details                  Detailed status of pgagroal\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
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
   char* database = NULL;
   struct configuration* config;

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
      ret = pgagroal_read_configuration("/etc/pgagroal/pgagroal.conf", shmem);
      if (ret)
      {
         printf("pgagroal-cli: Configuration not found: /etc/pgagroal/pgagroal.conf\n");
         exit(1);
      }
   }

   pgagroal_start_logging(shmem);
   config = (struct configuration*)shmem;

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
      else if (!strcmp("enable", argv[argc - 1]) || !strcmp("enable", argv[argc - 2]))
      {
         action = ACTION_ENABLEDB;
         if (!strcmp("enable", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
      }
      else if (!strcmp("disable", argv[argc - 1]) || !strcmp("disable", argv[argc - 2]))
      {
         action = ACTION_DISABLEDB;
         if (!strcmp("disable", argv[argc - 1]))
         {
            database = "*";
         }
         else
         {
            database = argv[argc - 1];
         }
      }
      else if (!strcmp("gracefully", argv[argc - 1]))
      {
         action = ACTION_GRACEFULLY;
      }
      else if (!strcmp("stop", argv[argc - 1]))
      {
         action = ACTION_STOP;
      }
      else if (!strcmp("status", argv[argc - 1]))
      {
         action = ACTION_STATUS;
      }
      else if (!strcmp("details", argv[argc - 1]))
      {
         action = ACTION_DETAILS;
      }
      else if (!strcmp("is-alive", argv[argc - 1]))
      {
         action = ACTION_ISALIVE;
      }
      else if (!strcmp("cancel-shutdown", argv[argc - 1]))
      {
         action = ACTION_CANCELSHUTDOWN;
      }

      if (action == ACTION_FLUSH)
      {
         if (pgagroal_management_flush(shmem, mode))
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_ENABLEDB)
      {
         if (pgagroal_management_enabledb(shmem, database))
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_DISABLEDB)
      {
         if (pgagroal_management_disabledb(shmem, database))
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_GRACEFULLY)
      {
         if (pgagroal_management_gracefully(shmem))
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_STOP)
      {
         if (pgagroal_management_stop(shmem))
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_CANCELSHUTDOWN)
      {
         if (pgagroal_management_cancel_shutdown(shmem))
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_STATUS)
      {
         int socket;

         if (pgagroal_management_status(shmem, &socket) == 0)
         {
            pgagroal_management_read_status(socket);
            pgagroal_disconnect(socket);
         }
         else
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_DETAILS)
      {
         int socket;

         if (pgagroal_management_details(shmem, &socket) == 0)
         {
            pgagroal_management_read_status(socket);
            pgagroal_management_read_details(socket);
            pgagroal_disconnect(socket);
         }
         else
         {
            exit_code = 1;
         }
      }
      else if (action == ACTION_ISALIVE)
      {
         int socket;
         int status = -1;

         if (pgagroal_management_isalive(shmem, &socket) == 0)
         {
            if (pgagroal_management_read_isalive(socket, &status))
            {
               exit_code = 1;
            }

            if (status != 1 && status != 2)
            {
               exit_code = 1;
            }

            pgagroal_disconnect(socket);
         }
         else
         {
            exit_code = 1;
         }
      }
   }

   if (action == ACTION_UNKNOWN)
   {
      usage();
      exit_code = 1;
   }
   
   if (action != ACTION_UNKNOWN && exit_code != 0)
   {
      printf("No connection to pgagroal on %s\n", config->unix_socket_dir);
   }

   pgagroal_stop_logging(shmem);

   munmap(shmem, size);

   return exit_code;
}
