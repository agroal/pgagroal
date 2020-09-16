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
#include <pipeline.h>
#include <security.h>
#include <utils.h>

#define ZF_LOG_TAG "configuration"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define LINE_LENGTH 512

static void extract_key_value(char* str, char** key, char** value);
static int as_int(char* str, int* i);
static int as_bool(char* str, bool* b);
static int as_logging_type(char* str);
static int as_logging_level(char* str);
static int as_validation(char* str);
static int as_pipeline(char* str);
static int as_hugepage(char* str);
static int extract_value(char* str, int offset, char** value);
static void extract_hba(char* str, char** type, char** database, char** user, char** address, char** method);
static void extract_limit(char* str, int server_max, char** database, char** user, int* max_size, int* initial_size, int* min_size);

/**
 *
 */
int
pgagroal_init_configuration(void* shmem, size_t size)
{
   struct configuration* config;

   config = (struct configuration*)shmem;
   memset(config, 0, size);

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

   config->buffer_size = DEFAULT_BUFFER_SIZE;
   config->keep_alive = true;
   config->nodelay = true;
   config->non_blocking = true;
   config->backlog = -1;
   config->hugepage = HUGEPAGE_TRY;

   config->log_type = PGAGROAL_LOGGING_TYPE_CONSOLE;
   config->log_level = PGAGROAL_LOGGING_LEVEL_INFO;
   config->log_connections = false;
   config->log_disconnections = false;

   config->max_connections = 100;
   config->allow_unknown_users = true;

   atomic_init(&config->su_connection, STATE_FREE);

   for (int i = 0; i < HISTOGRAM_BUCKETS; i++)
   {
      atomic_init(&config->prometheus.session_time[i], 0);
   }
   atomic_init(&config->prometheus.session_time_sum, 0);

   atomic_init(&config->prometheus.connection_error, 0);
   atomic_init(&config->prometheus.connection_kill, 0);
   atomic_init(&config->prometheus.connection_remove, 0);
   atomic_init(&config->prometheus.connection_timeout, 0);
   atomic_init(&config->prometheus.connection_return, 0);
   atomic_init(&config->prometheus.connection_invalid, 0);
   atomic_init(&config->prometheus.connection_get, 0);
   atomic_init(&config->prometheus.connection_idletimeout, 0);
   atomic_init(&config->prometheus.connection_flush, 0);
   atomic_init(&config->prometheus.connection_success, 0);

   atomic_init(&config->prometheus.auth_user_success, 0);
   atomic_init(&config->prometheus.auth_user_bad_password, 0);
   atomic_init(&config->prometheus.auth_user_error, 0);

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      atomic_init(&config->prometheus.server_error[i], 0);
   }
   atomic_init(&config->prometheus.failed_servers, 0);

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
   int idx_server = 0;
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
               else if (!strcmp(key, "max_connections"))
               {
                  if (!strcmp(section, "pgagroal"))
                  {
                     if (as_int(value, &config->max_connections))
                     {
                        unknown = true;
                     }
                     if (config->max_connections > MAX_NUMBER_OF_CONNECTIONS)
                     {
                        config->max_connections = MAX_NUMBER_OF_CONNECTIONS;
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
pgagroal_validate_configuration(bool has_unix_socket, bool has_main_sockets, void* shmem)
{
   bool tls;
   struct stat st;
   struct configuration* config;

   tls = false;

   config = (struct configuration*)shmem;

   if (!has_main_sockets)
   {
      if (strlen(config->host) == 0)
      {
         ZF_LOGF("pgagroal: No host defined");
         return 1;
      }

      if (config->port <= 0)
      {
         ZF_LOGF("pgagroal: No port defined");
         return 1;
      }
   }

   if (!has_unix_socket)
   {
      if (strlen(config->unix_socket_dir) == 0)
      {
         ZF_LOGF("pgagroal: No unix_socket_dir defined");
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

   if (config->authquery && strlen(config->superuser.username) == 0)
   {
      ZF_LOGF("pgagroal: Authentication query requires a superuser");
      return 1;
   }

   if (config->max_connections <= 0)
   {
      ZF_LOGF("pgagroal: max_connections must be greater than 0");
      return 1;
   }

   if (config->max_connections > MAX_NUMBER_OF_CONNECTIONS)
   {
      config->max_connections = MAX_NUMBER_OF_CONNECTIONS;
   }

   if (config->failover)
   {
      if (strlen(config->failover_script) == 0)
      {
         ZF_LOGF("pgagroal: Failover requires a script definition");
         return 1;
      }

      memset(&st, 0, sizeof(struct stat));

      if (stat(config->failover_script, &st) == -1)
      {
         ZF_LOGE("pgagroal: Can't locate failover script: %s", config->failover_script);
         return 1;
      }

      if (!S_ISREG(st.st_mode))
      {
         ZF_LOGE("pgagroal: Failover script is not a regular file: %s", config->failover_script);
         return 1;
      }

      if (st.st_uid != geteuid())
      {
         ZF_LOGE("pgagroal: Failover script not owned by user: %s", config->failover_script);
         return 1;
      }

      if (!(st.st_mode & (S_IRUSR | S_IXUSR)))
      {
         ZF_LOGE("pgagroal: Failover script must be executable: %s", config->failover_script);
         return 1;
      }

      if (config->number_of_servers <= 1)
      {
         ZF_LOGF("pgagroal: Failover requires at least 2 servers defined");
         return 1;
      }
   }

   if (config->number_of_servers <= 0)
   {
      ZF_LOGF("pgagroal: No servers defined");
      return 1;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (strlen(config->servers[i].host) == 0)
      {
         ZF_LOGF("pgagroal: No host defined for %s", config->servers[i].name);
         return 1;
      }

      if (config->servers[i].port == 0)
      {
         ZF_LOGF("pgagroal: No port defined for %s", config->servers[i].name);
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
   else if (config->pipeline == PIPELINE_TRANSACTION)
   {
      if (config->disconnect_client > 0)
      {
         ZF_LOGF("pgagroal: Transaction pipeline does not support disconnect_client");
         return 1;
      }

      if (config->blocking_timeout > 0)
      {
         ZF_LOGW("pgagroal: Using blocking_timeout for the transaction pipeline is not recommended");
      }

      if (config->idle_timeout > 0)
      {
         ZF_LOGW("pgagroal: Using idle_timeout for the transaction pipeline is not recommended");
      }

      if (config->validation == VALIDATION_FOREGROUND)
      {
         ZF_LOGW("pgagroal: Using foreground validation for the transaction pipeline is not recommended");
      }

      if (config->number_of_users == 0)
      {
         ZF_LOGI("pgagroal: Defining users for the transaction pipeline is recommended");
      }

      if (config->number_of_limits == 0)
      {
         ZF_LOGI("pgagroal: Defining limits for the transaction pipeline is recommended");
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
         ZF_LOGF("pgagroal: Performance pipeline does not support failover");
         return 1;
      }

      if (tls)
      {
         ZF_LOGF("pgagroal: Performance pipeline does not support TLS");
         return 1;
      }

      if (config->disconnect_client > 0)
      {
         ZF_LOGF("pgagroal: Performance pipeline does not support disconnect_client");
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_hba_configuration(char* filename, void* shmem)
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
      return 1;

   index = 0;
   config = (struct configuration*)shmem;

   while (fgets(line, sizeof(line), file))
   {
      if (strcmp(line, ""))
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
pgagroal_validate_hba_configuration(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->number_of_hbas == 0)
   {
      ZF_LOGF("pgagroal: No HBA entry defined");
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
         ZF_LOGF("pgagroal: Unknown HBA type: %s", config->hbas[i].type);
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
         ZF_LOGF("pgagroal: Unknown HBA method: %s", config->hbas[i].method);
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_limit_configuration(char* filename, void* shmem)
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
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
      return 1;

   index = 0;
   config = (struct configuration*)shmem;

   server_max = config->max_connections;

   while (fgets(line, sizeof(line), file))
   {
      if (strcmp(line, ""))
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
                  else if (initial_size < 0)
                  {
                     initial_size = 0;
                  }

                  if (min_size > max_size)
                  {
                     min_size = max_size;
                  }
                  else if (min_size < 0)
                  {
                     min_size = 0;
                  }

                  memcpy(&(config->limits[index].database), database, strlen(database));
                  memcpy(&(config->limits[index].username), username, strlen(username));
                  config->limits[index].max_size = max_size;
                  config->limits[index].initial_size = initial_size;
                  config->limits[index].min_size = min_size;
                  atomic_init(&config->limits[index].active_connections, 0);

                  server_max -= max_size;

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
pgagroal_validate_limit_configuration(void* shmem)
{
   int total_connections;
   struct configuration* config;

   total_connections = 0;
   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_limits; i++)
   {
      total_connections += config->limits[i].max_size;

      if (config->limits[i].max_size <= 0)
      {
         ZF_LOGF("max_size must be greater than 0 for limit entry %d", i);
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
            ZF_LOGF("Unknown user '%s' for limit entry %d", config->limits[i].username, i);
            return 1;
         }

         if (config->limits[i].initial_size < config->limits[i].min_size)
         {
            ZF_LOGW("initial_size smaller than min_size for limit entry (%d)", i);
            config->limits[i].initial_size = config->limits[i].min_size;
         }
      }
   }

   if (total_connections > config->max_connections)
   {
      ZF_LOGF("pgagroal: LIMIT: Too many connections defined %d (max %d)", total_connections, config->max_connections);
      return 1;
   }

   return 0;
}

/**
 *
 */
int
pgagroal_read_users_configuration(char* filename, void* shmem)
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
   config = (struct configuration*)shmem;

   while (fgets(line, sizeof(line), file))
   {
      if (strcmp(line, ""))
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
pgagroal_validate_users_configuration(void* shmem)
{
   return 0;
}

/**
 *
 */
int
pgagroal_read_admins_configuration(char* filename, void* shmem)
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
   config = (struct configuration*)shmem;

   while (fgets(line, sizeof(line), file))
   {
      if (strcmp(line, ""))
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
pgagroal_validate_admins_configuration(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->management > 0 && config->number_of_admins == 0)
   {
      ZF_LOGW("pgagroal: Remote management enabled, but no admins are defined");
   }

   return 0;
}

int
pgagroal_read_superuser_configuration(char* filename, void* shmem)
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
   config = (struct configuration*)shmem;

   while (fgets(line, sizeof(line), file))
   {
      if (strcmp(line, ""))
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

   return 0;
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

   if (!strcmp("all", value))
   {
      *max_size = server_max;
   }
   else
   {
      if (as_int(value, max_size))
      {
         printf("Invalid max_size value: %s\n", value);
         return;
      }
   }

   free(value);
   value = NULL;

   offset = extract_value(str, offset, &value);

   if (offset == -1)
      return;

   if (value != NULL && strcmp("", value) != 0 && as_int(value, initial_size))
   {
      printf("Invalid initial_size value: %s\n", value);
      return;
   }

   free(value);
   value = NULL;

   offset = extract_value(str, offset, &value);

   if (offset == -1)
      return;

   if (value != NULL && strcmp("", value) != 0 && as_int(value, min_size))
   {
      printf("Invalid min_size value: %s\n", value);
      return;
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
