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
#include <utils.h>

/* system */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_LENGTH 256

static void extract_key_value(char* str, char** key, char** value);
static int as_int(char* str);
static bool as_bool(char* str);
static int as_logging_type(char* str);
static int as_logging_level(char* str);
static int as_validation(char* str);

/**
 *
 */
int
pgagroal_init_configuration(void* shmem, size_t size)
{
   struct configuration* config;

   config = (struct configuration*)shmem;
   memset(config, 0, size);

   atomic_init(&config->number_of_connections, 0);
   
   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      config->servers[i].primary = SERVER_NOTINIT;
   }

   config->idle_timeout = 0;
   config->validation = VALIDATION_OFF;
   config->background_interval = 300;

   config->buffer_size = DEFAULT_BUFFER_SIZE;
   config->keep_alive = true;
   config->nodelay = true;
   config->non_blocking = true;
   config->backlog = DEFAULT_BACKLOG;

   config->log_type = PGAGROAL_LOGGING_TYPE_CONSOLE;
   config->log_level = PGAGROAL_LOGGING_LEVEL_INFO;

   return 0;
}

/**
 *
 */
int
pgagroal_read_configuration(char* filename, void* shmem)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* key = NULL;
   char* value = NULL;
   char* ptr = NULL;
   size_t max;
   struct configuration* config;
   int idx_server = -1;
   struct server srv;

   file = fopen(filename, "r");

   if (!file)
      return 1;
    
   memset(&section, 0, LINE_LENGTH);
   config = (struct configuration*)shmem;

   while (fgets(line, sizeof(line), file))
   {
      if (strcmp(line, ""))
      {
         if (line[0] == '[')
         {
            ptr = strchr(line, ']');
            if (ptr)
            {
               memset(&section, 0, LINE_LENGTH);
               max = ptr - line - 1;
               if (max > MISC_LENGTH - 1)
                  max = MISC_LENGTH - 1;
               memcpy(&section, line + 1, max);
               if (strcmp(section, "pgagroal"))
               {
                  idx_server++;

                  if (idx_server > 0 && idx_server < NUMBER_OF_SERVERS)
                  {
                     memcpy(&(config->servers[idx_server]), &srv, sizeof(struct server));
                  }
                  else if (idx_server >= NUMBER_OF_SERVERS)
                  {
                     printf("Maximum number of servers exceeded\n");
                  }

                  memset(&srv, 0, sizeof(struct server));
               }
            }
         }
         else if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            extract_key_value(line, &key, &value);

            if (key && value)
            {
               /* printf("|%s|%s|\n", key, value); */

               if (!strcmp(key, "host"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->host, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(&srv.host, value, max);
                     srv.primary = SERVER_NOTINIT;
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "port"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->port = as_int(value);
                  }
                  else if (strlen(section) > 0)
                  {
                     memcpy(&srv.name, section, strlen(section));
                     srv.port = as_int(value);
                     srv.primary = SERVER_NOTINIT;
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "primary"))
               {
                  if (strcmp(section, "pgagroal") && strlen(section) > 0)
                  {
                     if (as_bool(value))
                     {
                        srv.primary = SERVER_NOTINIT_PRIMARY;
                     }
                     else
                     {
                        srv.primary = SERVER_NOTINIT;
                     }
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "idle_timeout"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->idle_timeout = as_int(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "validation"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->validation = as_validation(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "background_interval"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->background_interval = as_int(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "log_type"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->log_type = as_logging_type(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "log_level"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->log_level = as_logging_level(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "log_path"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->log_path, value, max);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "max_connections"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->max_connections = as_int(value);
                     if (config->max_connections > NUMBER_OF_CONNECTIONS)
                     {
                        config->max_connections = NUMBER_OF_CONNECTIONS;
                     }
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "unix_socket_dir"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->unix_socket_dir, value, max);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "libev"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->libev, value, max);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "buffer_size"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->buffer_size = as_int(value);

                     if (config->buffer_size > MAX_BUFFER_SIZE)
                     {
                        config->buffer_size = MAX_BUFFER_SIZE;
                     }
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "keep_alive"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->keep_alive = as_bool(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "nodelay"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->nodelay = as_bool(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else if (!strcmp(key, "non_blocking"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->non_blocking = as_bool(value);
                  }
                  else
                  {
                     printf("Unknown: Section=<unknown>, Key=%s, Value=%s\n", key, value);
                  }
               }
               else
               {
                  printf("Unknown: Section=%s, Key=%s, Value=%s\n", strlen(section) > 0 ? section : "<unknown>", key, value);
               }

               free(key);
               free(value);
               key = NULL;
               value = NULL;
            }
         }
      }
   }

   if (idx_server != -1 && strlen(srv.name) > 0)
      memcpy(&(config->servers[idx_server]), &srv, sizeof(struct server));

   fclose(file);

   return 0;
}

/**
 *
 */
int
pgagroal_read_hba_configuration(char* filename, void* shmem)
{
   return 0;
}

static void
extract_key_value(char* str, char** key, char** value)
{
   int c = 0;
   int offset = 0;
   int length = strlen(str);
   char* k;
   char* v;

   while (str[c] != ' ' && str[c] != '=' && c < length)
      c++;

   if (c < length)
   {
      k = malloc(c + 1);
      memset(k, 0, c + 1);
      memcpy(k, str, c);
      *key = k;

      while ((str[c] == ' ' || str[c] == '\t' || str[c] == '=') && c < length)
         c++;

      offset = c;

      while (str[c] != ' ' && str[c] != '\n' && c < length)
         c++;

      if (c < length)
      {
         v = malloc((c - offset) + 1);
         memset(v, 0, (c - offset) + 1);
         memcpy(v, str + offset, (c - offset));
         *value = v;
      }
   }
}

static int as_int(char* str)
{
   return atoi(str);
}

static bool as_bool(char* str)
{
   if (!strcasecmp(str, "true"))
      return true;

   if (!strcasecmp(str, "on"))
      return true;

   if (!strcasecmp(str, "1"))
      return true;

   if (!strcasecmp(str, "false"))
      return false;

   if (!strcasecmp(str, "off"))
      return false;

   if (!strcasecmp(str, "0"))
      return false;

   return false;
}

static int as_logging_type(char* str)
{
   if (!strcasecmp(str, "console"))
      return PGAGROAL_LOGGING_TYPE_CONSOLE;

   if (!strcasecmp(str, "file"))
      return PGAGROAL_LOGGING_TYPE_FILE;

   if (!strcasecmp(str, "syslog"))
      return PGAGROAL_LOGGING_TYPE_SYSLOG;

   return 0;
}

static int as_logging_level(char* str)
{
   if (!strcasecmp(str, "debug5"))
      return PGAGROAL_LOGGING_LEVEL_DEBUG5;

   if (!strcasecmp(str, "debug4"))
      return PGAGROAL_LOGGING_LEVEL_DEBUG4;

   if (!strcasecmp(str, "debug3"))
      return PGAGROAL_LOGGING_LEVEL_DEBUG3;

   if (!strcasecmp(str, "debug2"))
      return PGAGROAL_LOGGING_LEVEL_DEBUG2;

   if (!strcasecmp(str, "debug1"))
      return PGAGROAL_LOGGING_LEVEL_DEBUG1;

   if (!strcasecmp(str, "info"))
      return PGAGROAL_LOGGING_LEVEL_INFO;

   if (!strcasecmp(str, "warn"))
      return PGAGROAL_LOGGING_LEVEL_WARN;

   if (!strcasecmp(str, "error"))
      return PGAGROAL_LOGGING_LEVEL_ERROR;

   if (!strcasecmp(str, "fatal"))
      return PGAGROAL_LOGGING_LEVEL_FATAL;

   return PGAGROAL_LOGGING_LEVEL_INFO;
}

static int as_validation(char* str)
{
   if (!strcasecmp(str, "off"))
      return VALIDATION_OFF;

   if (!strcasecmp(str, "foreground"))
      return VALIDATION_FOREGROUND;

   if (!strcasecmp(str, "background"))
      return VALIDATION_BACKGROUND;

   return VALIDATION_OFF;
}
