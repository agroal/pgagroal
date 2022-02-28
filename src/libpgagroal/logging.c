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
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define LINE_LENGTH 32

FILE *log_file;

time_t next_log_rotation_age;  /* number of seconds at which the next location will happen */

char current_log_path[MAX_PATH]; /* the current log file */

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

bool
log_rotation_enabled(void)
{
   struct configuration* config;
   config = (struct configuration*)shmem;

   // disable log rotation in the case
   // logging is not to a file
   if (config->log_type != PGAGROAL_LOGGING_TYPE_FILE)
   {
      log_rotation_disable();
      return false;
   }

   // log rotation is enabled if either log_rotation_age or
   // log_rotation_size is enabled
   return config->log_rotation_age != PGAGROAL_LOGGING_ROTATION_DISABLED
      || config->log_rotation_size != PGAGROAL_LOGGING_ROTATION_DISABLED;
}

void
log_rotation_disable(void)
{
   struct configuration* config;
   config = (struct configuration*)shmem;

   config->log_rotation_age  = PGAGROAL_LOGGING_ROTATION_DISABLED;
   config->log_rotation_size = PGAGROAL_LOGGING_ROTATION_DISABLED;
   next_log_rotation_age = 0;
}

bool
log_rotation_required(void)
{
   struct stat log_stat;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!log_rotation_enabled())
   {
      return false;
   }

   if (stat(current_log_path, &log_stat))
   {
      return false;
   }

   if (config->log_rotation_size > 0 && log_stat.st_size >= config->log_rotation_size)
   {
      return true;
   }

   if (config->log_rotation_age > 0 && next_log_rotation_age > 0 && next_log_rotation_age <= log_stat.st_ctime)
   {
      return true;
   }

   return false;
}


bool
log_rotation_set_next_rotation_age(void)
{
   struct configuration* config;
   time_t now;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE && config->log_rotation_age > 0)
   {
      now = time(NULL);
      if (!now)
      {
         config->log_rotation_age = PGAGROAL_LOGGING_ROTATION_DISABLED;
         return false;
      }

      next_log_rotation_age = now + config->log_rotation_age;
      return true;
   }
   else
   {
      config->log_rotation_age = PGAGROAL_LOGGING_ROTATION_DISABLED;
      return false;
   }
}


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
      log_file_open();

      if (!log_file)
      {
         printf("Failed to open log file %s due to %s\n", strlen(config->log_path) > 0 ? config->log_path : "pgagroal.log", strerror(errno));
         errno = 0;
         log_rotation_disable();
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

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE && !log_file)
   {
      log_file_open();

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

int
log_file_open(void)
{
   struct configuration* config;
   time_t htime;
   struct tm *tm;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      htime = time( NULL );
      if (!htime)
      {
         log_file = NULL;
         return 1;
      }

      tm = localtime(&htime);
      if (tm == NULL )
      {
         log_file = NULL;
         return 1;
      }

      if (strftime(current_log_path, sizeof(current_log_path), config->log_path, tm) <= 0 )
      {
         // cannot parse the format string, fallback to default logging
         memcpy(current_log_path, "pgagroal.log", strlen("pgagroal.log"));
         log_rotation_disable();
      }

      log_file = fopen(current_log_path, config->log_mode == PGAGROAL_LOGGING_MODE_APPEND ? "a" : "w");

      if (!log_file)
         return 1;

      log_rotation_set_next_rotation_age();
      return 0;
   }

   return 1;
}

void
log_file_rotate(void)
{
   if (log_rotation_enabled())
   {
      fflush(log_file);
      fclose(log_file);
      log_file_open();
   }
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
pgagroal_log_line(int level, char *file, int line, char *fmt, ...)
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

	 if (config->log_line_prefix == NULL || strlen(config->log_line_prefix) == 0)
	 {
	    memcpy(config->log_line_prefix,PGAGROAL_LOGGING_DEFAULT_LOG_LINE_PREFIX,strlen(PGAGROAL_LOGGING_DEFAULT_LOG_LINE_PREFIX));
	 }

         va_start(vl, fmt);

         if (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
         {
            buf[strftime(buf, sizeof(buf), config->log_line_prefix, tm)] = '\0';
            fprintf(stdout, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
                    buf, colors[level - 1], levels[level - 1],
                    filename, line);
            vfprintf(stdout, fmt, vl);
            fprintf(stdout, "\n");
            fflush(stdout);
         }
         else if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
         {
            buf[strftime(buf, sizeof(buf), config->log_line_prefix, tm)] = '\0';
            fprintf(log_file, "%s %-5s %s:%d ",
                    buf, levels[level - 1], filename, line);
            vfprintf(log_file, fmt, vl);
            fprintf(log_file, "\n");
            fflush(log_file);

            if (log_rotation_required())
            {
               log_file_rotate();
            }
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
