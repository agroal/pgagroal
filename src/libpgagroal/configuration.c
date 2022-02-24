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
#include <configuration.h>
#include <logging.h>
#include <pipeline.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif
#include <ctype.h>

#define LINE_LENGTH 512

static void extract_key_value(char* str, char** key, char** value);
static int as_int(char* str, int* i);
static int as_bool(char* str, bool* b);
static int as_logging_type(char* str);
static int as_logging_level(char* str);
static int as_logging_mode(char* str);
static int as_logging_rotation_size(char* str, int* size);
static int as_logging_rotation_age(char* str, int* age);
static int as_validation(char* str);
static int as_pipeline(char* str);
static int as_hugepage(char* str);
static int extract_value(char* str, int offset, char** value);
static void extract_hba(char* str, char** type, char** database, char** user, char** address, char** method);
static void extract_limit(char* str, int server_max, char** database, char** user, int* max_size, int* initial_size, int* min_size);

static int transfer_configuration(struct configuration* config, struct configuration* reload);
static void copy_server(struct server* dst, struct server* src);
static void copy_hba(struct hba* dst, struct hba* src);
static void copy_user(struct user* dst, struct user* src);
static int restart_int(char* name, int e, int n);
static int restart_string(char* name, char* e, char* n);
static int restart_limit(char* name, struct configuration* config, struct configuration* reload);

static bool is_empty_string(char* s);

/**
 *
 */
int
pgagroal_init_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   atomic_init(&config->active_connections, 0);
   
   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      atomic_init(&config->servers[i].state, SERVER_NOTINIT);
   }

   config->failover = false;
   config->tls = false;
   config->gracefully = false;
   config->pipeline = PIPELINE_AUTO;
   config->authquery = false;

   config->blocking_timeout = 30;
   config->idle_timeout = 0;
   config->validation = VALIDATION_OFF;
   config->background_interval = 300;
   config->max_retries = 5;
   config->authentication_timeout = 5;
   config->disconnect_client = 0;
   config->disconnect_client_force = false;

   config->buffer_size = DEFAULT_BUFFER_SIZE;
   config->keep_alive = true;
   config->nodelay = true;
   config->non_blocking = false;
   config->backlog = -1;
   config->hugepage = HUGEPAGE_TRY;
   config->tracker = false;
   config->track_prepared_statements = false;

   config->log_type = PGAGROAL_LOGGING_TYPE_CONSOLE;
   config->log_level = PGAGROAL_LOGGING_LEVEL_INFO;
   config->log_connections = false;
   config->log_disconnections = false;
   config->log_mode = PGAGROAL_LOGGING_MODE_APPEND;
   atomic_init(&config->log_lock, STATE_FREE);

   config->max_connections = 100;
   config->allow_unknown_users = true;

   atomic_init(&config->su_connection, STATE_FREE);

   return 0;
}

/**
 *
 */
int
pgagroal_read_configuration(void* shm, char* filename)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* key = NULL;
   char* value = NULL;
   char* ptr = NULL;
   size_t max;
   struct configuration* config;
   int idx_server = 0;
   struct server srv;

   file = fopen(filename, "r");

   if (!file)
   {
      return 1;
   }

   memset(&section, 0, LINE_LENGTH);
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
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
                  if (idx_server > 0 && idx_server <= NUMBER_OF_SERVERS)
                  {
                     memcpy(&(config->servers[idx_server - 1]), &srv, sizeof(struct server));
                  }
                  else if (idx_server > NUMBER_OF_SERVERS)
                  {
                     printf("Maximum number of servers exceeded\n");
                  }

                  memset(&srv, 0, sizeof(struct server));
                  atomic_init(&srv.state, SERVER_NOTINIT);
                  memcpy(&srv.name, &section, strlen(section));
                  idx_server++;
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
               bool unknown = false;

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
                     atomic_store(&srv.state, SERVER_NOTINIT);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "port"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->port))
                     {
                        unknown = true;
                     }
                  }
                  else if (strlen(section) > 0)
                  {
                     memcpy(&srv.name, section, strlen(section));
                     if (as_int(value, &srv.port))
                     {
                        unknown = true;
                     }
                     atomic_store(&srv.state, SERVER_NOTINIT);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "primary"))
               {
                  if (strcmp(section, "pgagroal") && strlen(section) > 0)
                  {
                     bool b = false;
                     if (as_bool(value, &b))
                     {
                        unknown = true;
                     }
                     if (b)
                     {
                        atomic_store(&srv.state, SERVER_NOTINIT_PRIMARY);
                     }
                     else
                     {
                        atomic_store(&srv.state, SERVER_NOTINIT);
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->metrics))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "management"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->management))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "pipeline"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->pipeline = as_pipeline(value);

                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "failover"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->failover))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "failover_script"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->failover_script, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "auth_query"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->authquery))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->tls))
                     {
                        unknown = true;
                     }
                  }
                  else if (strlen(section) > 0)
                  {
                     if (as_bool(value, &srv.tls))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_ca_file"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->tls_ca_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_cert_file"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->tls_cert_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_key_file"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->tls_key_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "blocking_timeout"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->blocking_timeout))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "idle_timeout"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->idle_timeout))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
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
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "background_interval"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->background_interval))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "max_retries"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->max_retries))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "authentication_timeout"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->authentication_timeout))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "disconnect_client"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->disconnect_client))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "disconnect_client_force"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->disconnect_client_force))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "pidfile"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                        max = MISC_LENGTH - 1;
                     memcpy(config->pidfile, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "allow_unknown_users"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->allow_unknown_users))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
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
                     unknown = true;
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
                     unknown = true;
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
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_rotation_size"))
               {
                 if (!strcmp(section, "pgagroal"))
                 {
                   if (as_logging_rotation_size(value, &config->log_rotation_size))
                   {
                     unknown = true;
                   }
                 }
                 else
                 {
                   unknown = true;
                 }

               }
               else if (!strcmp(key, "log_rotation_age"))
               {
                 if (!strcmp(section, "pgagroal"))
                 {
                   if (as_logging_rotation_age(value, &config->log_rotation_age))
                   {
                     unknown = true;
                   }
                 }
                 else
                 {
                   unknown = true;
                 }

               }
               else if (!strcmp(key, "log_line_prefix"))
               {
                 if (!strcmp(section, "pgagroal"))
                 {
                    max = strlen(value);
                    if (max > MISC_LENGTH - 1)
                      max = MISC_LENGTH - 1;

                    memcpy(config->log_line_prefix, value, max);
                 }
                 else
                 {
                   unknown = true;
                 }

               }
               else if (!strcmp(key, "log_connections"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->log_connections))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_disconnections"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->log_disconnections))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_mode"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->log_mode = as_logging_mode(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "max_connections"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->max_connections))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
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
                     unknown = true;
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
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "buffer_size"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->buffer_size))
                     {
                        unknown = true;
                     }
                     if (config->buffer_size > MAX_BUFFER_SIZE)
                     {
                        config->buffer_size = MAX_BUFFER_SIZE;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "keep_alive"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->keep_alive))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "nodelay"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->nodelay))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "non_blocking"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->non_blocking))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "backlog"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->backlog))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "hugepage"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     config->hugepage = as_hugepage(value);

                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tracker"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->tracker))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "track_prepared_statements"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_bool(value, &config->track_prepared_statements))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else
               {
                  unknown = true;
               }

               if (unknown)
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

   if (strlen(srv.name) > 0)
   {
      memcpy(&(config->servers[idx_server - 1]), &srv, sizeof(struct server));
   }

   config->number_of_servers = idx_server;

   fclose(file);

   return 0;
}

/**
 *
 */
int
pgagroal_validate_configuration(void* shm, bool has_unix_socket, bool has_main_sockets)
{
   bool tls;
   struct stat st;
   struct configuration* config;

   tls = false;

   config = (struct configuration*)shm;

   if (!has_main_sockets)
   {
      if (strlen(config->host) == 0)
      {
         pgagroal_log_fatal("pgagroal: No host defined");
         return 1;
      }

      if (config->port <= 0)
      {
         pgagroal_log_fatal("pgagroal: No port defined");
         return 1;
      }
   }

   if (!has_unix_socket)
   {
      if (strlen(config->unix_socket_dir) == 0)
      {
         pgagroal_log_fatal("pgagroal: No unix_socket_dir defined");
         return 1;
      }

      if (stat(config->unix_socket_dir, &st) == 0 && S_ISDIR(st.st_mode))
      {
         /* Ok */
      }
      else
      {
         pgagroal_log_fatal("pgagroal: unix_socket_dir is not a directory (%s)", config->unix_socket_dir);
         return 1;
      }
   }

   if (config->backlog <= 0)
   {
      config->backlog = MAX(config->max_connections / 4, 16);
   }

   if (config->authentication_timeout <= 0)
   {
      config->authentication_timeout = 5;
   }

   if (config->disconnect_client <= 0)
   {
      config->disconnect_client = 0;
   }

   if (config->authquery)
   {
      if (strlen(config->superuser.username) == 0)
      {
         pgagroal_log_fatal("pgagroal: Authentication query requires a superuser");
         return 1;
      }
      else
      {
         config->allow_unknown_users = true;

         if (config->number_of_users > 0)
         {
            pgagroal_log_fatal("pgagroal: Users are not supported when using authentication query");
            return 1;
         }

         if (config->number_of_frontend_users > 0)
         {
            pgagroal_log_fatal("pgagroal: Frontend users are not supported when using authentication query");
            return 1;
         }

         if (config->number_of_limits > 0)
         {
            pgagroal_log_fatal("pgagroal: Limits are not supported when using authentication query");
            return 1;
         }
      }
   }

   if (config->max_connections <= 0)
   {
      pgagroal_log_fatal("pgagroal: max_connections must be greater than 0");
      return 1;
   }

   if (config->max_connections > MAX_NUMBER_OF_CONNECTIONS)
   {
      pgagroal_log_warn("pgagroal: max_connections (%d) is greater than allowed (%d)", config->max_connections, MAX_NUMBER_OF_CONNECTIONS);
      config->max_connections = MAX_NUMBER_OF_CONNECTIONS;
   }

   if (config->number_of_frontend_users > 0 && config->allow_unknown_users)
   {
      pgagroal_log_warn("pgagroal: Frontend users should not be used with allow_unknown_users");
   }

   if (config->failover)
   {
      if (strlen(config->failover_script) == 0)
      {
         pgagroal_log_fatal("pgagroal: Failover requires a script definition");
         return 1;
      }

      memset(&st, 0, sizeof(struct stat));

      if (stat(config->failover_script, &st) == -1)
      {
         pgagroal_log_error("pgagroal: Can't locate failover script: %s", config->failover_script);
         return 1;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgagroal_log_error("pgagroal: Failover script is not a regular file: %s", config->failover_script);
         return 1;
      }

      if (st.st_uid != geteuid())
      {
         pgagroal_log_error("pgagroal: Failover script not owned by user: %s", config->failover_script);
         return 1;
      }

      if (!(st.st_mode & (S_IRUSR | S_IXUSR)))
      {
         pgagroal_log_error("pgagroal: Failover script must be executable: %s", config->failover_script);
         return 1;
      }

      if (config->number_of_servers <= 1)
      {
         pgagroal_log_fatal("pgagroal: Failover requires at least 2 servers defined");
         return 1;
      }
   }

   if (config->number_of_servers <= 0)
   {
      pgagroal_log_fatal("pgagroal: No servers defined");
      return 1;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (strlen(config->servers[i].host) == 0)
      {
         pgagroal_log_fatal("pgagroal: No host defined for %s", config->servers[i].name);
         return 1;
      }

      if (config->servers[i].port == 0)
      {
         pgagroal_log_fatal("pgagroal: No port defined for %s", config->servers[i].name);
         return 1;
      }
   }

   if (config->pipeline == PIPELINE_AUTO)
   {
      if (config->tls && (strlen(config->tls_cert_file) > 0 || strlen(config->tls_key_file) > 0))
      {
         tls = true;
      }

      if (config->failover || tls || config->disconnect_client > 0)
      {
         config->pipeline = PIPELINE_SESSION;
      }
      else
      {
         config->pipeline = PIPELINE_PERFORMANCE;
      }
   }

   if (config->pipeline == PIPELINE_SESSION)
   {
      /* Checks */
   }
   else if (config->pipeline == PIPELINE_TRANSACTION)
   {
      if (config->disconnect_client > 0)
      {
         pgagroal_log_fatal("pgagroal: Transaction pipeline does not support disconnect_client");
         return 1;
      }

      if (!config->authquery)
      {
         if (config->number_of_users == 0)
         {
            pgagroal_log_fatal("pgagroal: Users must be defined for the transaction pipeline");
            return 1;
         }

         if (config->allow_unknown_users)
         {
            pgagroal_log_fatal("pgagroal: Transaction pipeline does not support allow_unknown_users");
            return 1;
         }

         if (config->number_of_limits == 0)
         {
            pgagroal_log_info("pgagroal: Defining limits for the transaction pipeline is recommended");
         }
      }

      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (config->servers[i].tls)
         {
            pgagroal_log_fatal("pgagroal: Transaction pipeline does not support TLS to a server");
            return 1;
         }
      }

      if (config->blocking_timeout > 0)
      {
         pgagroal_log_warn("pgagroal: Using blocking_timeout for the transaction pipeline is not recommended");
      }

      if (config->idle_timeout > 0)
      {
         pgagroal_log_warn("pgagroal: Using idle_timeout for the transaction pipeline is not recommended");
      }

      if (config->validation == VALIDATION_FOREGROUND)
      {
         pgagroal_log_warn("pgagroal: Using foreground validation for the transaction pipeline is not recommended");
      }
   }
   else if (config->pipeline == PIPELINE_PERFORMANCE)
   {
      if (config->tls && (strlen(config->tls_cert_file) > 0 || strlen(config->tls_key_file) > 0))
      {
         tls = true;
      }

      if (config->failover)
      {
         pgagroal_log_fatal("pgagroal: Performance pipeline does not support failover");
         return 1;
      }

      if (tls)
      {
         pgagroal_log_fatal("pgagroal: Performance pipeline does not support TLS");
         return 1;
      }

      if (config->disconnect_client > 0)
      {
         pgagroal_log_fatal("pgagroal: Performance pipeline does not support disconnect_client");
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_hba_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* type = NULL;
   char* database = NULL;
   char* username = NULL;
   char* address = NULL;
   char* method = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      return 1;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            extract_hba(line, &type, &database, &username, &address, &method);

            if (type && database && username && address && method)
            {
               if (strlen(type) < MAX_TYPE_LENGTH &&
                   strlen(database) < MAX_DATABASE_LENGTH &&
                   strlen(username) < MAX_USERNAME_LENGTH &&
                   strlen(address) < MAX_ADDRESS_LENGTH &&
                   strlen(method) < MAX_ADDRESS_LENGTH)
               {
                  memcpy(&(config->hbas[index].type), type, strlen(type));
                  memcpy(&(config->hbas[index].database), database, strlen(database));
                  memcpy(&(config->hbas[index].username), username, strlen(username));
                  memcpy(&(config->hbas[index].address), address, strlen(address));
                  memcpy(&(config->hbas[index].method), method, strlen(method));

                  index++;

                  if (index >= NUMBER_OF_HBAS)
                  {
                     printf("pgagroal: Too many HBA entries (%d)\n", NUMBER_OF_HBAS);
                     fclose(file);
                     return 2;
                  }
               }
               else
               {
                  printf("pgagroal: Invalid HBA entry\n");
                  printf("%s\n", line);
               }
            }
            else
            {
               printf("pgagroal: Invalid HBA entry\n");
               printf("%s\n", line);
            }

            free(type);
            free(database);
            free(username);
            free(address);
            free(method);

            type = NULL;
            database = NULL;
            username = NULL;
            address = NULL;
            method = NULL;
         }
      }
   }

   config->number_of_hbas = index;

   fclose(file);

   return 0;
}

/**
 *
 */
int
pgagroal_validate_hba_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   if (config->number_of_hbas == 0)
   {
      pgagroal_log_fatal("pgagroal: No HBA entry defined");
      return 1;
   }

   for (int i = 0; i < config->number_of_hbas; i++)
   {
      if (!strcasecmp("host", config->hbas[i].type) ||
          !strcasecmp("hostssl", config->hbas[i].type))
      {
         /* Ok */
      }
      else
      {
         pgagroal_log_fatal("pgagroal: Unknown HBA type: %s", config->hbas[i].type);
         return 1;
      }

      if (!strcasecmp("trust", config->hbas[i].method) ||
          !strcasecmp("reject", config->hbas[i].method) ||
          !strcasecmp("password", config->hbas[i].method) ||
          !strcasecmp("md5", config->hbas[i].method) ||
          !strcasecmp("scram-sha-256", config->hbas[i].method) ||
          !strcasecmp("all", config->hbas[i].method))
      {
         /* Ok */
      }
      else
      {
         pgagroal_log_fatal("pgagroal: Unknown HBA method: %s", config->hbas[i].method);
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_limit_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* database = NULL;
   char* username = NULL;
   int max_size;
   int initial_size;
   int min_size;
   int server_max;
   int lineno;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      return 1;
   }

   index = 0;
   lineno = 0;
   config = (struct configuration*)shm;

   server_max = config->max_connections;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            initial_size = 0;
            min_size = 0;

            extract_limit(line, server_max, &database, &username, &max_size, &initial_size, &min_size);

            if (database && username)
            {
               if (strlen(database) < MAX_DATABASE_LENGTH &&
                   strlen(username) < MAX_USERNAME_LENGTH)
               {
                  if (initial_size > max_size)
                  {
                     initial_size = max_size;
                  }

                  if (min_size > max_size)
                  {
                     min_size = max_size;
                  }

                  server_max -= max_size;

                  memcpy(&(config->limits[index].database), database, strlen(database));
                  memcpy(&(config->limits[index].username), username, strlen(username));
                  config->limits[index].max_size = max_size;
                  config->limits[index].initial_size = initial_size;
                  config->limits[index].min_size = min_size;
                  config->limits[index].lineno = ++lineno;
                  atomic_init(&config->limits[index].active_connections, 0);

                  index++;

                  if (index >= NUMBER_OF_LIMITS)
                  {
                     printf("pgagroal: Too many LIMIT entries (%d)\n", NUMBER_OF_LIMITS);
                     fclose(file);
                     return 2;
                  }
               }
               else
               {
                  printf("pgagroal: Invalid LIMIT entry\n");
                  printf("%s\n", line);
               }
            }
            else
            {
               printf("pgagroal: Invalid LIMIT entry\n");
               printf("%s\n", line);
            }

            free(database);
            free(username);

            database = NULL;
            username = NULL;
            max_size = 0;
         }
      }
   }

   config->number_of_limits = index;

   fclose(file);

   return 0;
}

/**
 *
 */
int
pgagroal_validate_limit_configuration(void* shm)
{
   int total_connections;
   struct configuration* config;

   total_connections = 0;
   config = (struct configuration*)shm;

   for (int i = 0; i < config->number_of_limits; i++)
   {
      total_connections += config->limits[i].max_size;

      if (config->limits[i].max_size <= 0)
      {
         pgagroal_log_fatal("max_size must be greater than 0 for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
         return 1;
      }

      if (config->limits[i].initial_size < 0)
      {
         pgagroal_log_fatal("initial_size must be greater or equal to 0 for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
         return 1;
      }

      if (config->limits[i].min_size < 0)
      {
         pgagroal_log_fatal("min_size must be greater or equal to 0 for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
         return 1;
      }

      if (config->limits[i].initial_size > 0 || config->limits[i].min_size > 0)
      {
         bool user_found = false;

         for (int j = 0; j < config->number_of_users; j++)
         {
            if (!strcmp(config->limits[i].username, config->users[j].username))
            {
               user_found = true;
            }
         }

         if (!user_found)
         {
            pgagroal_log_fatal("Unknown user '%s' for limit entry %d (%s:%d)", config->limits[i].username, i + 1, config->limit_path, config->limits[i].lineno);
            return 1;
         }

         if (config->limits[i].initial_size < config->limits[i].min_size)
         {
            pgagroal_log_warn("initial_size smaller than min_size for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
            config->limits[i].initial_size = config->limits[i].min_size;
         }
      }
   }

   if (total_connections > config->max_connections)
   {
      pgagroal_log_fatal("pgagroal: LIMIT: Too many connections defined %d (max_connections = %d)", total_connections, config->max_connections);
      return 1;
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_users_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   int decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      goto masterkey;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            ptr = strtok(line, ":");

            username = ptr;

            ptr = strtok(NULL, ":");

            if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
            {
               goto error;
            }

            if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
            {
               goto error;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               memcpy(&config->users[index].username, username, strlen(username));
               memcpy(&config->users[index].password, password, strlen(password));
            }
            else
            {
               printf("pgagroal: Invalid USER entry\n");
               printf("%s\n", line);
            }

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;
         }
      }
   }

   config->number_of_users = index;

   if (config->number_of_users > NUMBER_OF_USERS)
   {
      goto above;
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgagroal_validate_users_configuration(void* shm)
{
   return 0;
}

/**
 *
 */
int
pgagroal_read_frontend_users_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   int decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      goto masterkey;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            ptr = strtok(line, ":");

            username = ptr;

            ptr = strtok(NULL, ":");

            if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
            {
               goto error;
            }

            if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
            {
               goto error;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               memcpy(&config->frontend_users[index].username, username, strlen(username));
               memcpy(&config->frontend_users[index].password, password, strlen(password));
            }
            else
            {
               printf("pgagroal: Invalid FRONTEND USER entry\n");
               printf("%s\n", line);
            }

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;
         }
      }
   }

   config->number_of_frontend_users = index;

   if (config->number_of_frontend_users > NUMBER_OF_USERS)
   {
      goto above;
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgagroal_validate_frontend_users_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   for (int i = 0; i < config->number_of_frontend_users; i++)
   {
      bool found = false;
      char* f = &config->frontend_users[i].username[0];

      for (int i = 0; !found && i < config->number_of_users; i++)
      {
         char* u = &config->users[i].username[0];

         if (!strcmp(f, u))
         {
            found = true;
         }
      }

      if (!found)
      {
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_admins_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   int decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      goto masterkey;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            ptr = strtok(line, ":");

            username = ptr;

            ptr = strtok(NULL, ":");

            if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
            {
               goto error;
            }

            if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
            {
               goto error;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               memcpy(&config->admins[index].username, username, strlen(username));
               memcpy(&config->admins[index].password, password, strlen(password));
            }
            else
            {
               printf("pgagroal: Invalid ADMIN entry\n");
               printf("%s\n", line);
            }

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;
         }
      }
   }

   config->number_of_admins = index;

   if (config->number_of_admins > NUMBER_OF_ADMINS)
   {
      goto above;
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgagroal_validate_admins_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   if (config->management > 0 && config->number_of_admins == 0)
   {
      pgagroal_log_warn("pgagroal: Remote management enabled, but no admins are defined");
   }

   return 0;
}

int
pgagroal_read_superuser_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   int decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      goto masterkey;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            if (index > 0)
            {
               goto above;
            }

            ptr = strtok(line, ":");

            username = ptr;

            ptr = strtok(NULL, ":");

            if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
            {
               goto error;
            }

            if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
            {
               goto error;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               memcpy(&config->superuser.username, username, strlen(username));
               memcpy(&config->superuser.password, password, strlen(password));
            }
            else
            {
               printf("pgagroal: Invalid SUPERUSER entry\n");
               printf("%s\n", line);
            }

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;
         }
      }
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgagroal_validate_superuser_configuration(void* shm)
{
   return 0;
}

int
pgagroal_reload_configuration(void)
{
   size_t reload_size;
   struct configuration* reload = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_log_trace("Configuration: %s", config->configuration_path);
   pgagroal_log_trace("HBA: %s", config->hba_path);
   pgagroal_log_trace("Limit: %s", config->limit_path);
   pgagroal_log_trace("Users: %s", config->users_path);
   pgagroal_log_trace("Frontend users: %s", config->frontend_users_path);
   pgagroal_log_trace("Admins: %s", config->admins_path);
   pgagroal_log_trace("Superuser: %s", config->superuser_path);

   reload_size = sizeof(struct configuration);

   if (pgagroal_create_shared_memory(reload_size, HUGEPAGE_OFF, (void**)&reload))
   {
      goto error;
   }

   pgagroal_init_configuration((void*)reload);

   if (pgagroal_read_configuration((void*)reload, config->configuration_path))
   {
      goto error;
   }

   if (pgagroal_read_hba_configuration((void*)reload, config->hba_path))
   {
      goto error;
   }

   if (strcmp("", config->limit_path))
   {
      if (pgagroal_read_limit_configuration((void*)reload, config->limit_path))
      {
         goto error;
      }
   }

   if (strcmp("", config->users_path))
   {
      if (pgagroal_read_users_configuration((void*)reload, config->users_path))
      {
         goto error;
      }
   }

   if (strcmp("", config->frontend_users_path))
   {
      if (pgagroal_read_frontend_users_configuration((void*)reload, config->frontend_users_path))
      {
         goto error;
      }
   }

   if (strcmp("", config->admins_path))
   {
      if (pgagroal_read_admins_configuration((void*)reload, config->admins_path))
      {
         goto error;
      }
   }

   if (strcmp("", config->superuser_path))
   {
      if (pgagroal_read_superuser_configuration((void*)reload, config->superuser_path))
      {
         goto error;
      }
   }

   if (pgagroal_validate_configuration(reload, false, false))
   {
      goto error;
   }

   if (pgagroal_validate_hba_configuration(reload))
   {
      goto error;
   }

   if (pgagroal_validate_limit_configuration(reload))
   {
      goto error;
   }

   if (pgagroal_validate_users_configuration(reload))
   {
      goto error;
   }

   if (pgagroal_validate_frontend_users_configuration(reload))
   {
      goto error;
   }

   if (pgagroal_validate_admins_configuration(reload))
   {
      goto error;
   }

   if (pgagroal_validate_superuser_configuration(reload))
   {
      goto error;
   }

   if (transfer_configuration(config, reload))
   {
      goto error;
   }

   pgagroal_destroy_shared_memory((void*)reload, reload_size);

   pgagroal_log_debug("Reload: Success");

   return 0;

error:
   if (reload != NULL)
   {
      pgagroal_destroy_shared_memory((void*)reload, reload_size);
   }

   pgagroal_log_debug("Reload: Failure");

   return 1;
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

      while (str[c] != ' ' && str[c] != '\r' && str[c] != '\n' && c < length)
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

static int
as_int(char* str, int* i)
{
   char* endptr;
   long val;

   errno = 0;
   val = strtol(str, &endptr, 10);

   if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
   {
      goto error;
   }

   if (str == endptr)
   {
      goto error;
   }

   if (*endptr != '\0')
   {
      goto error;
   }

   *i = (int)val;

   return 0;

error:

   errno = 0;

   return 1;
}

static int
as_bool(char* str, bool* b)
{
   if (!strcasecmp(str, "true") || !strcasecmp(str, "on") || !strcasecmp(str, "1"))
   {
      *b = true;
      return 0;
   }

   if (!strcasecmp(str, "false") || !strcasecmp(str, "off") || !strcasecmp(str, "0"))
   {
      *b = false;
      return 0;
   }

   return 1;
}

static int
as_logging_type(char* str)
{
   if (!strcasecmp(str, "console"))
      return PGAGROAL_LOGGING_TYPE_CONSOLE;

   if (!strcasecmp(str, "file"))
      return PGAGROAL_LOGGING_TYPE_FILE;

   if (!strcasecmp(str, "syslog"))
      return PGAGROAL_LOGGING_TYPE_SYSLOG;

   return PGAGROAL_LOGGING_TYPE_CONSOLE;
}

static int
as_logging_level(char* str)
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

static int
as_logging_mode(char* str)
{
   if (!strcasecmp(str, "a") || !strcasecmp(str, "append"))
      return PGAGROAL_LOGGING_MODE_APPEND;

   if (!strcasecmp(str, "c") || !strcasecmp(str, "create"))
      return PGAGROAL_LOGGING_MODE_CREATE;

   return PGAGROAL_LOGGING_MODE_APPEND;
}

static int
as_validation(char* str)
{
   if (!strcasecmp(str, "off"))
      return VALIDATION_OFF;

   if (!strcasecmp(str, "foreground"))
      return VALIDATION_FOREGROUND;

   if (!strcasecmp(str, "background"))
      return VALIDATION_BACKGROUND;

   return VALIDATION_OFF;
}

static int
as_pipeline(char* str)
{
   if (!strcasecmp(str, "auto"))
      return PIPELINE_AUTO;

   if (!strcasecmp(str, "performance"))
      return PIPELINE_PERFORMANCE;

   if (!strcasecmp(str, "session"))
      return PIPELINE_SESSION;

   if (!strcasecmp(str, "transaction"))
      return PIPELINE_TRANSACTION;

   return PIPELINE_AUTO;
}

static int
as_hugepage(char* str)
{
   if (!strcasecmp(str, "off"))
      return HUGEPAGE_OFF;

   if (!strcasecmp(str, "try"))
      return HUGEPAGE_TRY;

   if (!strcasecmp(str, "on"))
      return HUGEPAGE_ON;

   return HUGEPAGE_OFF;
}

static void
extract_hba(char* str, char** type, char** database, char** user, char** address, char** method)
{
   int offset = 0;
   int length = strlen(str);

   offset = extract_value(str, offset, type);

   if (offset == -1 || offset >= length)
      return;

   offset = extract_value(str, offset, database);

   if (offset == -1 || offset >= length)
      return;

   offset = extract_value(str, offset, user);

   if (offset == -1 || offset >= length)
      return;

   offset = extract_value(str, offset, address);

   if (offset == -1 || offset >= length)
      return;

   extract_value(str, offset, method);
}

static void
extract_limit(char* str, int server_max, char** database, char** user, int* max_size, int* initial_size, int* min_size)
{
   int offset = 0;
   int length = strlen(str);
   char* value = NULL;

   *max_size = 0;
   *initial_size = 0;
   *min_size = 0;

   offset = extract_value(str, offset, database);

   if (offset == -1 || offset >= length)
      return;

   offset = extract_value(str, offset, user);

   if (offset == -1 || offset >= length)
      return;

   offset = extract_value(str, offset, &value);

   if (offset == -1)
      return;

   if (!strcasecmp("all", value))
   {
      *max_size = server_max;
   }
   else
   {
      if (as_int(value, max_size))
      {
         *max_size = -1;
         return;
      }
   }

   free(value);
   value = NULL;

   offset = extract_value(str, offset, &value);

   if (offset == -1)
      return;

   if (value != NULL && strcmp("", value) != 0)
   {
      if (!strcasecmp("all", value))
      {
         *initial_size = server_max;
      }
      else
      {
         if (as_int(value, initial_size))
         {
            *initial_size = -1;
            return;
         }
      }
   }

   free(value);
   value = NULL;

   offset = extract_value(str, offset, &value);

   if (offset == -1)
      return;

   if (value != NULL && strcmp("", value) != 0)
   {
      if (!strcasecmp("all", value))
      {
         *min_size = server_max;
      }
      else
      {
         if (as_int(value, min_size))
         {
            *min_size = -1;
            return;
         }
      }
   }

   free(value);
}

static int
extract_value(char* str, int offset, char** value)
{
   int from;
   int to;
   int length = strlen(str);
   char* v = NULL;

   while ((str[offset] == ' ' || str[offset] == '\t') && offset < length)
      offset++;

   if (offset < length)
   {
      from = offset;

      while ((str[offset] != ' ' && str[offset] != '\t' && str[offset] != '\r' && str[offset] != '\n') && offset < length)
         offset++;

      if (offset < length)
      {
         to = offset;

         v = malloc(to - from + 1);
         memset(v, 0, to - from + 1);
         memcpy(v, str + from, to - from);
         *value = v;

         return offset;
      }
   }

   return -1;
}

static int
transfer_configuration(struct configuration* config, struct configuration* reload)
{
#ifdef HAVE_LINUX
   sd_notify(0, "RELOADING=1");
#endif

   memcpy(config->host, reload->host, MISC_LENGTH);
   config->port = reload->port;
   config->metrics = reload->metrics;
   config->management = reload->management;
   /* gracefully */

   /* disabled */

   /* pipeline */
   restart_int("pipeline", config->pipeline, reload->pipeline);

   config->failover = reload->failover;
   memcpy(config->failover_script, reload->failover_script, MISC_LENGTH);

   /* log_type */
   restart_int("log_type", config->log_type, reload->log_type);
   config->log_level = reload->log_level;
   /* log_path */
   restart_string("log_path", config->log_path, reload->log_path);
   config->log_connections = reload->log_connections;
   config->log_disconnections = reload->log_disconnections;
   restart_int("log_mode", config->log_mode, reload->log_mode);
   /* log_lock */

   config->authquery = reload->authquery;

   config->tls = reload->tls;
   memcpy(config->tls_cert_file, reload->tls_cert_file, MISC_LENGTH);
   memcpy(config->tls_key_file, reload->tls_key_file, MISC_LENGTH);
   memcpy(config->tls_ca_file, reload->tls_ca_file, MISC_LENGTH);

   if (config->tls && (config->pipeline == PIPELINE_SESSION || config->pipeline == PIPELINE_TRANSACTION))
   {
      if (pgagroal_tls_valid())
      {
         pgagroal_log_fatal("pgagroal: Invalid TLS configuration");
         exit(1);
      }
   }

   /* active_connections */
   /* max_connections */
   restart_int("max_connections", config->max_connections, reload->max_connections);
   config->allow_unknown_users = reload->allow_unknown_users;

   config->blocking_timeout = reload->blocking_timeout;
   config->idle_timeout = reload->idle_timeout;
   config->validation = reload->validation;
   config->background_interval = reload->background_interval;
   config->max_retries = reload->max_retries;
   config->authentication_timeout = reload->authentication_timeout;
   config->disconnect_client = reload->disconnect_client;
   config->disconnect_client_force = reload->disconnect_client_force;
   /* pidfile */
   restart_string("pidfile", config->pidfile, reload->pidfile);

   /* libev */
   restart_string("libev", config->libev, reload->libev);
   config->buffer_size = reload->buffer_size;
   config->keep_alive = reload->keep_alive;
   config->nodelay = reload->nodelay;
   config->non_blocking = reload->non_blocking;
   config->backlog = reload->backlog;
   /* hugepage */
   restart_int("hugepage", config->hugepage, reload->hugepage);
   config->tracker = reload->tracker;
   config->track_prepared_statements = reload->track_prepared_statements;

   /* unix_socket_dir */
   restart_string("unix_socket_dir", config->unix_socket_dir, reload->unix_socket_dir);

   /* su_connection */

   /* states */

   memset(&config->servers[0], 0, sizeof(struct server) * NUMBER_OF_SERVERS);
   for (int i = 0; i < reload->number_of_servers; i++)
   {
      copy_server(&config->servers[i], &reload->servers[i]);
   }
   config->number_of_servers = reload->number_of_servers;

   memset(&config->hbas[0], 0, sizeof(struct hba) * NUMBER_OF_HBAS);
   for (int i = 0; i < reload->number_of_hbas; i++)
   {
      copy_hba(&config->hbas[i], &reload->hbas[i]);
   }
   config->number_of_hbas = reload->number_of_hbas;

   /* number_of_limits */
   /* limits */
   restart_limit("limits", config, reload);

   memset(&config->users[0], 0, sizeof(struct user) * NUMBER_OF_USERS);
   for (int i = 0; i < reload->number_of_users; i++)
   {
      copy_user(&config->users[i], &reload->users[i]);
   }
   config->number_of_users = reload->number_of_users;

   memset(&config->frontend_users[0], 0, sizeof(struct user) * NUMBER_OF_USERS);
   for (int i = 0; i < reload->number_of_frontend_users; i++)
   {
      copy_user(&config->frontend_users[i], &reload->frontend_users[i]);
   }
   config->number_of_frontend_users = reload->number_of_frontend_users;

   memset(&config->admins[0], 0, sizeof(struct user) * NUMBER_OF_ADMINS);
   for (int i = 0; i < reload->number_of_admins; i++)
   {
      copy_user(&config->admins[i], &reload->admins[i]);
   }
   config->number_of_admins = reload->number_of_admins;

   memset(&config->superuser, 0, sizeof(struct user));
   copy_user(&config->superuser, &reload->superuser);

   /* prometheus */
   /* connections[] */

#ifdef HAVE_LINUX
   sd_notify(0, "READY=1");
#endif

   return 0;
}

static void
copy_server(struct server* dst, struct server* src)
{
   memcpy(&dst->name[0], &src->name[0], MISC_LENGTH);
   memcpy(&dst->host[0], &src->host[0], MISC_LENGTH);
   dst->port = src->port;
   atomic_init(&dst->state, SERVER_NOTINIT);
}

static void
copy_hba(struct hba* dst, struct hba* src)
{
   memcpy(&dst->type[0], &src->type[0], MAX_TYPE_LENGTH);
   memcpy(&dst->database[0], &src->database[0], MAX_DATABASE_LENGTH);
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->address[0], &src->address[0], MAX_ADDRESS_LENGTH);
   memcpy(&dst->method[0], &src->method[0], MAX_ADDRESS_LENGTH);
}

static void
copy_user(struct user* dst, struct user* src)
{
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->password[0], &src->password[0], MAX_PASSWORD_LENGTH);
}

static int
restart_int(char* name, int e, int n)
{
   if (e != n)
   {
      pgagroal_log_info("Restart required for %s - Existing %d New %d", name, e, n);
      return 1;
   }

   return 0;
}

static int
restart_string(char* name, char* e, char* n)
{
   if (strcmp(e, n))
   {
      pgagroal_log_info("Restart required for %s - Existing %s New %s", name, e, n);
      return 1;
   }

   return 0;
}

static int
restart_limit(char* name, struct configuration* config, struct configuration* reload)
{
   int ret;

   ret = restart_int("limits", config->number_of_limits, reload->number_of_limits);
   if (ret == 1)
   {
      goto error;
   }

   for (int i = 0; i < reload->number_of_limits; i++)
   {
      struct limit* e;
      struct limit* n;

      e = &config->limits[i];
      n = &reload->limits[i];

      if (strcmp(e->database, n->database) ||
          strcmp(e->username, n->username) ||
          e->max_size != n->max_size ||
          e->initial_size != n->initial_size ||
          e->min_size != n->min_size)
      {
         pgagroal_log_info("Restart required for limits");
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static bool
is_empty_string(char* s)
{
   if (s == NULL)
   {
      return true;
   }

   if (!strcmp(s, ""))
   {
      return true;
   }

   for (int i = 0; i < strlen(s); i++)
   {
      if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
      {
         /* Ok */
      }
      else
      {
         return false;
      }
   }

   return true;
}


/**
 * Parses a string to see if it contains
 * a valid value for log rotation size.
 * Returns 0 if parsing ok, 1 otherwise.
 *
 * Valid strings have one of the suffixes:
 * - k for kilobytes
 * - m for megabytes
 * - g for gigabytes
 *
 * The default is expressed always as kilobytes. The functions sets the
 * rotation size in kilobytes.
 */
static int
as_logging_rotation_size(char* str, int* size)
{
  int multiplier = 1;
  int index;
  char value[MISC_LENGTH];
  bool multiplier_set = false;
  
  if (is_empty_string(str))
  {
    *size = PGAGROAL_LOGGING_ROTATION_DISABLED;
    return 0;
  }

  index = 0;
  for (int i = 0; i < strlen(str); i++)
  {
    if (isdigit(str[i]))
    {
      value[index++] = str[i];
    }
    else if (isalpha(str[i]) && multiplier_set)
    {
      *size = PGAGROAL_LOGGING_ROTATION_DISABLED;
      return 1; 
    }
    else if (isalpha(str[i]) && ! multiplier_set)
    {
      
      if (str[i] == 'M' || str[i] == 'm')
      {
        multiplier = 1024 * 1024;
        multiplier_set = true;
      }
      else if(str[i] == 'G' || str[i] == 'g')
      {
        multiplier = 1024 * 1024 * 1024;
        multiplier_set = true;
      }
      else if(str[i] == 'K' || str[i] == 'k')
      {
        multiplier = 1024;
        multiplier_set = true;
      }
      else if(str[i] == 'B' || str[i] == 'b')
      {
        multiplier = 1;
        multiplier_set = true;
      }
    }
    else
      // ignore alien chars
      continue;
  }

  value[index] = '\0';
  if (!as_int(value, size))
  {
    *size = *size * multiplier;
    return 0;
  }
  else
  {
      *size = PGAGROAL_LOGGING_ROTATION_DISABLED;
      return 1;
  }

}


/**
 * Parses the log_rotation_age string.
 * The string accepts
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in seconds.
 * The function sets the number of rotationg age as minutes.
 * Returns 1 for errors, 0 for correct parsing.
 */
static int
as_logging_rotation_age(char* str, int* age)
{
  int multiplier = 1;
  int index;
  char value[MISC_LENGTH];
  bool multiplier_set = false;
  
  if (is_empty_string(str))
  {
    *age = PGAGROAL_LOGGING_ROTATION_DISABLED;
    return 0;
  }

  index = 0;
  for (int i = 0; i < strlen(str); i++)
  {
    if (isdigit(str[i]))
    {
      value[index++] = str[i];
    }
    else if (isalpha(str[i]) && multiplier_set)
    {
      *age = PGAGROAL_LOGGING_ROTATION_DISABLED;
      return 1;
    }
    else if (isalpha(str[i]) && ! multiplier_set)
    {
      if (str[i] == 's' || str[i] == 'S')
      {
        multiplier = 1;
        multiplier_set = true;
      }
      else if (str[i] == 'm' || str[i] == 'M')
      {
        multiplier = 60;
        multiplier_set = true;
      }
      else if (str[i] == 'h' || str[i] == 'H')
      {
        multiplier = 3600;
        multiplier_set = true;
      }
      else if (str[i] == 'd' || str[i] == 'D')
      {
        multiplier = 24 * 3600;
        multiplier_set = true;
      }
      else if (str[i] == 'w' || str[i] == 'W')
      {
        multiplier = 24 * 3600 * 7;
        multiplier_set = true;
      }
    }
    else
      continue;
  }

  value[index] = '\0';
  if (!as_int(value, age))
  {
    *age = *age * multiplier;
    return 0;
  }
  else
  {
      *age = PGAGROAL_LOGGING_ROTATION_DISABLED;
      return 1;
  }
}
