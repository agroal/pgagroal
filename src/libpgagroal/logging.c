/*
 * Copyright (C) 2022 Red Hat
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

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define LINE_LENGTH 32

FILE* log_file;

static const char* levels[] =
{
   "TRACE",
   "DEBUG",
   "INFO",
   "WARN",
   "ERROR",
   "FATAL"
};

static const char* colors[] =
{
   "\x1b[37m",
   "\x1b[36m",
   "\x1b[32m",
   "\x1b[91m",
   "\x1b[31m",
   "\x1b[35m"
};

/**
 *
 */
int
pgagroal_init_logging(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      if (strlen(config->log_path) > 0)
      {
         if (config->log_mode == PGAGROAL_LOGGING_MODE_APPEND)
         {
            log_file = fopen(config->log_path, "a");
         }
         else
         {
            log_file = fopen(config->log_path, "w");
         }
      }
      else
      {
         if (config->log_mode == PGAGROAL_LOGGING_MODE_APPEND)
         {
            log_file = fopen("pgagroal.log", "a");
         }
         else
         {
            log_file = fopen("pgagroal.log", "w");
         }
      }

      if (!log_file)
      {
         printf("Failed to open log file %s due to %s\n", strlen(config->log_path) > 0 ? config->log_path : "pgagroal.log", strerror(errno));
         errno = 0;
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_start_logging(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      if (strlen(config->log_path) > 0)
      {
         log_file = fopen(config->log_path, "a");
      }
      else
      {
         log_file = fopen("pgagroal.log", "a");
      }

      if (!log_file)
      {
         printf("Failed to open log file %s due to %s\n", strlen(config->log_path) > 0 ? config->log_path : "pgagroal.log", strerror(errno));
         errno = 0;
         return 1;
      }
   }
   else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
   {
      openlog("pgagroal", LOG_CONS | LOG_PERROR | LOG_PID, LOG_USER);
   }

   return 0;
}

/**
 *
 */
int
pgagroal_stop_logging(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      if (log_file != NULL)
      {
         return fclose(log_file);
      }
      else
      {
         return 1;
      }
   }
   else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
   {
      closelog();
   }

   return 0;
}

void
pgagroal_log_line(int level, char* file, int line, char* fmt, ...)
{
   signed char isfree;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config == NULL)
      return;

   if (level >= config->log_level)
   {
retry:
      isfree = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->log_lock, &isfree, STATE_IN_USE))
      {
         char buf[256];
         va_list vl;
         struct tm* tm;
         time_t t;
         char* filename;

         t = time(NULL);
         tm = localtime(&t);

         filename = strrchr(file, '/');
         if (filename != NULL)
         {
            filename = filename + 1;
         }
         else
         {
            filename = file;
         }

         va_start(vl, fmt);

         if (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
         {
            buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm)] = '\0';
            fprintf(stdout, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
                    buf, colors[level - 1], levels[level - 1],
                    filename, line);
            vfprintf(stdout, fmt, vl);
            fprintf(stdout, "\n");
            fflush(stdout);
         }
         else if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
         {
            buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm)] = '\0';
            fprintf(log_file, "%s %-5s %s:%d ",
                    buf, levels[level - 1], filename, line);
            vfprintf(log_file, fmt, vl);
            fprintf(log_file, "\n");
            fflush(log_file);
         }
         else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
         {
            switch (level)
            {
               case PGAGROAL_LOGGING_LEVEL_DEBUG5:
                  vsyslog(LOG_DEBUG, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_DEBUG1:
                  vsyslog(LOG_DEBUG, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_INFO:
                  vsyslog(LOG_INFO, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_WARN:
                  vsyslog(LOG_WARNING, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_ERROR:
                  vsyslog(LOG_ERR, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_FATAL:
                  vsyslog(LOG_CRIT, fmt, vl);
                  break;
               default:
                  vsyslog(LOG_INFO, fmt, vl);
                  break;
            }
         }

         va_end(vl);

         atomic_store(&config->log_lock, STATE_FREE);
      }
      else
      {
         /* Sleep for 1ms */
         struct timespec ts;
         ts.tv_sec = 0;
         ts.tv_nsec = 1000000L;
         nanosleep(&ts, NULL);

         goto retry;
      }
   }
}

void
pgagroal_log_mem(void* data, size_t size)
{
   signed char isfree;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config == NULL)
      return;

   if (config->log_level == PGAGROAL_LOGGING_LEVEL_DEBUG5 &&
       size > 0 &&
       (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE || config->log_type == PGAGROAL_LOGGING_TYPE_FILE))
   {
retry:
      isfree = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->log_lock, &isfree, STATE_IN_USE))
      {
         char buf[256 * 1024];
         int j = 0;
         int k = 0;

         memset(&buf, 0, sizeof(buf));

         for (int i = 0; i < size; i++)
         {
            if (k == LINE_LENGTH)
            {
               buf[j] = '\n';
               j++;
               k = 0;
            }
            sprintf(&buf[j], "%02X", (signed char) *((char*)data + i));
            j += 2;
            k++;
         }

         buf[j] = '\n';
         j++;
         k = 0;

         for (int i = 0; i < size; i++)
         {
            signed char c = (signed char) *((char*)data + i);
            if (k == LINE_LENGTH)
            {
               buf[j] = '\n';
               j++;
               k = 0;
            }
            if (c >= 32 && c <= 127)
            {
               buf[j] = c;
            }
            else
            {
               buf[j] = '?';
            }
            j++;
            k++;
         }

         if (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
         {
            fprintf(stdout, "%s", buf);
            fprintf(stdout, "\n");
            fflush(stdout);
         }
         else if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
         {
            fprintf(log_file, "%s", buf);
            fprintf(log_file, "\n");
            fflush(log_file);
         }

         atomic_store(&config->log_lock, STATE_FREE);
      }
      else
      {
         /* Sleep for 1ms */
         struct timespec ts;
         ts.tv_sec = 0;
         ts.tv_nsec = 1000000L;
         nanosleep(&ts, NULL);

         goto retry;
      }
   }
}
