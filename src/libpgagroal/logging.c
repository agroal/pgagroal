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
#include <zf_log.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

FILE *log_file;

void
file_callback(const zf_log_message *msg, void *arg)
{
   (void)arg;
   *msg->p = '\n';
   fwrite(msg->buf, msg->p - msg->buf + 1, 1, log_file);
   fflush(log_file);
}

int
syslog_level(const int lvl)
{
   switch (lvl)
   {
      /* case PGAGROAL_LOGGING_LEVEL_DEBUG5: */
      /* case PGAGROAL_LOGGING_LEVEL_DEBUG4: */
      /* case PGAGROAL_LOGGING_LEVEL_DEBUG3: */
      case PGAGROAL_LOGGING_LEVEL_DEBUG2:
      case PGAGROAL_LOGGING_LEVEL_DEBUG1:
         return LOG_DEBUG;
      case PGAGROAL_LOGGING_LEVEL_INFO:
         return LOG_INFO;
      case PGAGROAL_LOGGING_LEVEL_WARN:
         return LOG_WARNING;
      case PGAGROAL_LOGGING_LEVEL_ERROR:
         return LOG_ERR;
      case PGAGROAL_LOGGING_LEVEL_FATAL:
         return LOG_EMERG;
      default:
         return PGAGROAL_LOGGING_LEVEL_FATAL;
   }
}

void
syslog_callback(const zf_log_message *msg, void *arg)
{
   (void)arg;
   /* p points to the log message end. By default, message is not terminated
    * with 0, but it has some space allocated for EOL area, so there is always
    * some place for terminating zero in the end (see ZF_LOG_EOL_SZ define in
    * zf_log.c).
    */
   *msg->p = 0;
   syslog(syslog_level(msg->lvl), "%s", msg->tag_b);
}

/**
 *
 */
int
pgagroal_start_logging(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   zf_log_set_tag_prefix("pgagroal");
   zf_log_set_output_level(config->log_level);

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
         ZF_LOGW("Failed to open log file %s due to %s", strlen(config->log_path) > 0 ? config->log_path : "pgagroal.log", strerror(errno));
         errno = 0;
         return 1;
      }

      zf_log_set_output_v(ZF_LOG_PUT_STD, 0, file_callback);
   }
   else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
   {
      openlog("pgagroal", LOG_CONS | LOG_PERROR | LOG_PID, LOG_USER);

      const unsigned put_mask = ZF_LOG_PUT_STD & !ZF_LOG_PUT_CTX;
      zf_log_set_output_v(put_mask, 0, syslog_callback);
   }

   return 0;
}

/**
 *
 */
int
pgagroal_stop_logging(void* shmem)
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
