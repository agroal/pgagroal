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
#include <configuration.h>
#include <logging.h>
#include <pipeline.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <prometheus.h>

/* system */
#include <ctype.h>
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
#include <err.h>
#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif

#define LINE_LENGTH 512

static int extract_key_value(char* str, char** key, char** value);
static int as_int(char* str, int* i);
static int as_bool(char* str, bool* b);
static int as_logging_type(char* str);
static int as_logging_level(char* str);
static int as_logging_mode(char* str);

static int as_logging_rotation_size(char* str, unsigned int* size);
static int as_logging_rotation_age(char* str, unsigned int* age);
static int as_validation(char* str);
static int as_pipeline(char* str);
static int as_hugepage(char* str);
static unsigned int as_update_process_title(char* str, unsigned int* policy, unsigned int default_policy);
static int extract_value(char* str, int offset, char** value);
static void extract_hba(char* str, char** type, char** database, char** user, char** address, char** method);
static void extract_limit(char* str, int server_max, char** database, char** user, int* max_size, int* initial_size, int* min_size);
static unsigned int as_seconds(char* str, unsigned int* age, unsigned int default_age);
static unsigned int as_bytes(char* str, unsigned int* bytes, unsigned int default_bytes);

static int transfer_configuration(struct main_configuration* config, struct main_configuration* reload);
static void copy_server(struct server* dst, struct server* src);
static void copy_hba(struct hba* dst, struct hba* src);
static void copy_user(struct user* dst, struct user* src);
static int restart_int(char* name, int e, int n);
static int restart_bool(char* name, bool e, bool n);
static int restart_string(char* name, char* e, char* n, bool skip_non_existing);
static int restart_limit(char* name, struct main_configuration* config, struct main_configuration* reload);
static int restart_server(struct server* src, struct server* dst);

static bool is_empty_string(char* s);
static bool is_same_server(struct server* s1, struct server* s2);
static bool is_same_tls(struct server* s1, struct server* s2);

static bool key_in_section(char* wanted, char* section, char* key, bool global, bool* unknown);
static bool is_comment_line(char* line);
static bool section_line(char* line, char* section);

static int pgagroal_write_server_config_value(char* buffer, char* server_name, char* config_key, size_t buffer_size);
static int pgagroal_write_hba_config_value(char* buffer, char* username, char* config_key, size_t buffer_size);
static int pgagroal_write_limit_config_value(char* buffer, char* database, char* config_key, size_t buffer_size);
static int pgagroal_apply_hba_configuration(struct hba* hba, char* context, char* value);
static int pgagroal_apply_limit_configuration_string(struct limit* limit, char* context, char* value);
static int pgagroal_apply_limit_configuration_int(struct limit* limit, char* context, int value);

static int to_string(char* where, char* value, size_t max_length);
static int to_bool(char* where, bool value);
static int to_int(char* where, int value);
static int to_update_process_title(char* where, int value);
static int to_validation(char* where, int value);
static int to_pipeline(char* where, int value);
static int to_log_mode(char* where, int value);
static int to_log_level(char* where, int value);
static int to_log_type(char* where, int value);

/**
 *
 */
int
pgagroal_init_configuration(void* shm)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shm;

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
   config->rotate_frontend_password_timeout = 0;
   config->rotate_frontend_password_length = MIN_PASSWORD_LENGTH;
   config->max_connection_age = 0;
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

   config->common.log_type = PGAGROAL_LOGGING_TYPE_CONSOLE;
   config->common.log_level = PGAGROAL_LOGGING_LEVEL_INFO;
   config->common.log_connections = false;
   config->common.log_disconnections = false;
   config->common.log_mode = PGAGROAL_LOGGING_MODE_APPEND;
   atomic_init(&config->common.log_lock, STATE_FREE);

   memcpy(config->common.default_log_path, "pgagroal.log", strlen("pgagroal.log"));

   config->max_connections = 100;
   config->allow_unknown_users = true;

   atomic_init(&config->su_connection, STATE_FREE);

   config->update_process_title = UPDATE_PROCESS_TITLE_VERBOSE;

   return 0;
}

/**
 * This struct is going to store the metadata
 * about which sections have been parsed during
 * the configuration read.
 * This can be used to seek for duplicated sections
 * at different positions in the configuration file.
 */
struct config_section
{
   char name[LINE_LENGTH];  /**< The name of the section */
   unsigned int lineno;     /**< The line number for this section */
   bool main;               /**< Is this the main configuration section or a server one? */
};

/**
 *
 */
int
pgagroal_read_configuration(void* shm, char* filename, bool emit_warnings)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* key = NULL;
   char* value = NULL;
   struct main_configuration* config;
   int idx_server = 0;
   struct server srv;
   bool has_main_section = false;

   // the max number of sections allowed in the configuration
   // file is done by the max number of servers plus the main `pgagroal`
   // configuration section
   struct config_section sections[NUMBER_OF_SERVERS + 1];
   int idx_sections = 0;
   int lineno = 0;
   int return_value = 0;

   file = fopen(filename, "r");

   if (!file)
   {
      return PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
   }

   memset(&section, 0, LINE_LENGTH);
   memset(&sections, 0, sizeof(struct config_section) * NUMBER_OF_SERVERS + 1);
   config = (struct main_configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      lineno++;

      if (!is_empty_string(line) && !is_comment_line(line))
      {
         if (section_line(line, section))
         {
            // check we don't overflow the number of available sections
            if (idx_sections >= NUMBER_OF_SERVERS + 1)
            {
               warnx("Max number of sections (%d) in configuration file <%s> reached!",
                     NUMBER_OF_SERVERS + 1,
                     filename);
               return PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
            }

            // initialize the section structure
            memset(sections[idx_sections].name, 0, LINE_LENGTH);
            memcpy(sections[idx_sections].name, section, strlen(section));
            sections[idx_sections].lineno = lineno;
            sections[idx_sections].main = !strncmp(section, PGAGROAL_MAIN_INI_SECTION, LINE_LENGTH);
            if (sections[idx_sections].main)
            {
               has_main_section = true;
            }

            idx_sections++;

            if (strcmp(section, PGAGROAL_MAIN_INI_SECTION))
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
               srv.lineno = lineno;
               idx_server++;
            }
         }
         else
         {
            extract_key_value(line, &key, &value);

            if (key && value)
            {
               bool unknown = false;

               //printf("\nSection <%s> key <%s> = <%s>", section, key, value);

               // apply the configuration setting
               if (pgagroal_apply_main_configuration(config, &srv, section, key, value))
               {
                  unknown = true;
               }

               if (unknown && emit_warnings)
               {
                  // we cannot use logging here...
                  // if we have a section, the key is not known,
                  // otherwise it is outside of a section at all
                  if (strlen(section) > 0)
                  {
                     warnx("Unknown key <%s> with value <%s> in section [%s] (line %d of file <%s>)",
                           key,
                           value,
                           section,
                           lineno,
                           filename);
                  }
                  else
                  {
                     warnx("Key <%s> with value <%s> out of any section (line %d of file <%s>)",
                           key,
                           value,
                           lineno,
                           filename);
                  }
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

   // check there is at least one main section
   if (!has_main_section)
   {
      warnx("No main configuration section [%s] found in file <%s>",
            PGAGROAL_MAIN_INI_SECTION,
            filename);
      return PGAGROAL_CONFIGURATION_STATUS_KO;
   }

   // validate the sections:
   // do a nested loop to scan over all the sections that have a duplicated
   // name and warn the user about them.
   for (int i = 0; i < NUMBER_OF_SERVERS + 1; i++)
   {
      for (int j = i + 1; j < NUMBER_OF_SERVERS + 1; j++)
      {
         // skip uninitialized sections
         if (!strlen(sections[i].name) || !strlen(sections[j].name))
         {
            continue;
         }

         if (!strncmp(sections[i].name, sections[j].name, LINE_LENGTH))
         {
            // cannot log here ...
            warnx("%s section [%s] duplicated at lines %d and %d of file <%s>",
                  sections[i].main ? "Main" : "Server",
                  sections[i].name,
                  sections[i].lineno,
                  sections[j].lineno,
                  filename);
            return_value++;    // this is an error condition!
         }
      }
   }

   return return_value;
}

/**
 *
 */
int
pgagroal_validate_configuration(void* shm, bool has_unix_socket, bool has_main_sockets)
{
   bool tls;
   struct stat st;
   struct main_configuration* config;

   tls = false;

   config = (struct main_configuration*)shm;

   if (!has_main_sockets)
   {
      if (strlen(config->common.host) == 0)
      {
         pgagroal_log_fatal("pgagroal: No host defined");
         return 1;
      }

      if (config->common.port <= 0)
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

   if (config->rotate_frontend_password_length < MIN_PASSWORD_LENGTH || config->rotate_frontend_password_length > MAX_PASSWORD_LENGTH)
   {
      pgagroal_log_fatal("pgagroal: rotate_frontend_password_length should be within [8-1024] characters");
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
         pgagroal_log_fatal("pgagroal: No host defined for server [%s] (%s:%d)",
                            config->servers[i].name,
                            config->common.configuration_path[0],
                            config->servers[i].lineno);
         return 1;
      }

      if (config->servers[i].port == 0)
      {
         pgagroal_log_fatal("pgagroal: No port defined for server [%s] (%s:%d)",
                            config->servers[i].name,
                            config->common.configuration_path[0],
                            config->servers[i].lineno);
         return 1;
      }
   }

   // check for duplicated servers
   for (int i = 0; i < config->number_of_servers; i++)
   {
      for (int j = i + 1; j < config->number_of_servers; j++)
      {
         if (is_same_server(&config->servers[i], &config->servers[j]))
         {
            pgagroal_log_fatal("pgagroal: Servers [%s] and [%s] are duplicated! (%s:%d:%d)",
                               config->servers[i].name,
                               config->servers[j].name,
                               config->common.configuration_path[0],
                               config->servers[i].lineno,
                               config->servers[j].lineno);
            return 1;
         }
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
      }

      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (config->servers[i].tls)
         {
            pgagroal_log_fatal("pgagroal: Transaction pipeline does not support TLS to a server");
            return 1;
         }
      }

      if (config->number_of_limits == 0)
      {
         pgagroal_log_fatal("pgagroal: Defining limits for the transaction pipeline is mandatory");
         return 1;
      }

      for (int i = 0; i < config->number_of_limits; i++)
      {
         if (config->limits[i].min_size <= 0)
         {
            pgagroal_log_fatal("pgagroal: min_size for transaction pipeline must be greater than 0");
            return 1;
         }

         if (config->limits[i].initial_size <= 0)
         {
            pgagroal_log_fatal("pgagroal: initial_size for transaction pipeline must be greater than 0");
            return 1;
         }

         if (config->limits[i].max_size <= 0)
         {
            pgagroal_log_fatal("pgagroal: max_size for transaction pipeline must be greater than 0");
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

      if (config->rotate_frontend_password_timeout > 0)
      {
         pgagroal_log_warn("pgagroal: Using rotate_frontend_password_timeout for the transaction pipeline is not recommended");
      }

      if (config->max_connection_age > 0)
      {
         pgagroal_log_warn("pgagroal: Using max_connection_age for the transaction pipeline is not recommended");
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

   // do some last initialization here, since the configuration
   // looks good so far
   pgagroal_init_pidfile_if_needed();

   return 0;
}

/**
 *
 */
int
pgagroal_vault_init_configuration(void* shm)
{
   struct vault_configuration* config;

   config = (struct vault_configuration*)shm;

   config->common.port = 0;

   config->vault_server.server.port = 0;
   config->vault_server.server.tls = false;
   config->number_of_users = 0;

   config->common.log_type = PGAGROAL_LOGGING_TYPE_CONSOLE;
   config->common.log_level = PGAGROAL_LOGGING_LEVEL_INFO;
   config->common.log_connections = false;
   config->common.log_disconnections = false;
   config->common.log_mode = PGAGROAL_LOGGING_MODE_APPEND;
   atomic_init(&config->common.log_lock, STATE_FREE);
   memcpy(config->common.default_log_path, "pgagroal-vault.log", strlen("pgagroal-vault.log"));

   memset(config->vault_server.user.password, 0, MAX_PASSWORD_LENGTH);

   return 0;
}

/**
 *
 */
int
pgagroal_vault_read_configuration(void* shm, char* filename, bool emit_warnings)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* key = NULL;
   char* value = NULL;
   struct vault_configuration* config;
   int idx_server = 0;
   struct vault_server srv;
   bool has_vault_section = false;

   // the max number of sections allowed in the configuration
   // file is done by the max number of servers plus the main `pgagroal`
   // configuration section
   struct config_section sections[1 + 1];
   int idx_sections = 0;
   int lineno = 0;
   int return_value = 0;

   file = fopen(filename, "r");

   if (!file)
   {
      return PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
   }

   memset(&section, 0, LINE_LENGTH);
   memset(&sections, 0, sizeof(struct config_section) * 2);
   config = (struct vault_configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      lineno++;
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         if (section_line(line, section))
         {
            // check we don't overflow the number of available sections
            if (idx_sections >= 2)
            {
               warnx("Max number of sections (%d) in configuration file <%s> reached!",
                     2,
                     filename);
               return PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
            }

            // initialize the section structure
            memset(sections[idx_sections].name, 0, LINE_LENGTH);
            memcpy(sections[idx_sections].name, section, strlen(section));
            sections[idx_sections].lineno = lineno;
            sections[idx_sections].main = !strncmp(section, PGAGROAL_VAULT_INI_SECTION, LINE_LENGTH);
            if (sections[idx_sections].main)
            {
               has_vault_section = true;
            }

            idx_sections++;

            if (strcmp(section, PGAGROAL_VAULT_INI_SECTION))
            {
               if (idx_server > 0 && idx_server <= 2)
               {
                  memcpy(&(config->vault_server), &srv, sizeof(struct vault_server));
               }
               else if (idx_server > 1)
               {
                  printf("Maximum number of servers exceeded\n");
               }

               memset(&srv, 0, sizeof(struct vault_server));
               memcpy(&srv.server.name, &section, strlen(section));
               srv.server.lineno = lineno;
               idx_server++;
            }
         }
         else
         {
            extract_key_value(line, &key, &value);

            if (key && value)
            {
               bool unknown = false;

               //printf("\nSection <%s> key <%s> = <%s>", section, key, value);

               // apply the configuration setting
               if (pgagroal_apply_vault_configuration(config, &srv, section, key, value))
               {
                  unknown = true;
               }

               if (unknown && emit_warnings)
               {
                  // we cannot use logging here...
                  // if we have a section, the key is not known,
                  // otherwise it is outside of a section at all
                  if (strlen(section) > 0)
                  {
                     warnx("Unknown key <%s> with value <%s> in section [%s] (line %d of file <%s>)",
                           key,
                           value,
                           section,
                           lineno,
                           filename);
                  }
                  else
                  {
                     warnx("Key <%s> with value <%s> out of any section (line %d of file <%s>)",
                           key,
                           value,
                           lineno,
                           filename);
                  }
               }

               free(key);
               free(value);
               key = NULL;
               value = NULL;
            }
         }
      }
   }

   if (strlen(srv.server.name) > 0)
   {
      memcpy(&(config->vault_server), &srv, sizeof(struct vault_server));
   }

   fclose(file);

   // check there is at least one main section
   if (!has_vault_section)
   {
      warnx("No vault configuration section [%s] found in file <%s>",
            PGAGROAL_VAULT_INI_SECTION,
            filename);
      return PGAGROAL_CONFIGURATION_STATUS_KO;
   }

   return return_value;
}

/**
 *
 */
int
pgagroal_vault_validate_configuration (void* shm)
{
   struct vault_configuration* config;
   config = (struct vault_configuration*)shm;

   if (strlen(config->common.host) == 0)
   {
      pgagroal_log_fatal("pgagroal-vault: No host defined");
      return 1;
   }

   if (config->common.port <= 0)
   {
      pgagroal_log_fatal("pgagroal-vault: No port defined");
      return 1;
   }

   if (strlen(config->vault_server.server.host) == 0)
   {
      pgagroal_log_fatal("pgagroal-vault: No host defined for server [%s] (%s:%d)",
                         config->vault_server.server.name,
                         config->common.configuration_path,
                         config->vault_server.server.lineno);
      return 1;
   }

   if (config->vault_server.server.port == 0)
   {
      pgagroal_log_fatal("pgagroal-vault: No port defined for server [%s] (%s:%d)",
                         config->vault_server.server.name,
                         config->common.configuration_path,
                         config->vault_server.server.lineno);
      return 1;
   }

   if (strlen(config->vault_server.user.username) == 0)
   {
      pgagroal_log_fatal("pgagroal-vault: No user defined for server [%s] (%s:%d)",
                         config->vault_server.server.name,
                         config->common.configuration_path,
                         config->vault_server.server.lineno);
      return 1;
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
   int lineno = 0;
   struct main_configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      return PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
   }

   index = 0;
   config = (struct main_configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      lineno++;

      if (!is_empty_string(line) && !is_comment_line(line))
      {
         extract_hba(line, &type, &database, &username, &address, &method);

         if (pgagroal_apply_hba_configuration(&config->hbas[index], PGAGROAL_HBA_ENTRY_TYPE, type) == 0
             && pgagroal_apply_hba_configuration(&config->hbas[index], PGAGROAL_HBA_ENTRY_DATABASE, database) == 0
             && pgagroal_apply_hba_configuration(&config->hbas[index], PGAGROAL_HBA_ENTRY_USERNAME, username) == 0
             && pgagroal_apply_hba_configuration(&config->hbas[index], PGAGROAL_HBA_ENTRY_ADDRESS, address) == 0
             && pgagroal_apply_hba_configuration(&config->hbas[index], PGAGROAL_HBA_ENTRY_METHOD, method) == 0)
         {
            // ok, this configuration has been applied
            index++;

            if (index >= NUMBER_OF_HBAS)
            {
               warnx("Too many HBA entries (max is %d)\n", NUMBER_OF_HBAS);
               fclose(file);
               return PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
            }
         }
         else
         {
            warnx("Invalid HBA entry (%s:%d)", filename, lineno);
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

   config->number_of_hbas = index;

   fclose(file);

   return PGAGROAL_CONFIGURATION_STATUS_OK;
}

/**
 *
 */
int
pgagroal_validate_hba_configuration(void* shm)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shm;

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
         pgagroal_log_fatal("Unknown HBA type: %s (%s:%d)", config->hbas[i].type, config->hba_path, config->hbas[i].lineno);
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
         pgagroal_log_fatal("Unknown HBA method: %s (%s:%d)", config->hbas[i].method, config->hba_path, config->hbas[i].lineno);
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
   struct main_configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      return PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
   }

   index = 0;
   lineno = 0;
   config = (struct main_configuration*)shm;

   server_max = config->max_connections;

   while (fgets(line, sizeof(line), file))
   {
      lineno++;

      if (!is_empty_string(line) && !is_comment_line(line))
      {
         initial_size = 0;
         min_size = 0;

         extract_limit(line, server_max, &database, &username, &max_size, &initial_size, &min_size);
         lineno++;

         if (database && username)
         {

            // normalize the sizes
            initial_size = initial_size > max_size ? max_size : initial_size;
            min_size = min_size > max_size ? max_size : min_size;

            if (pgagroal_apply_limit_configuration_string(&config->limits[index], PGAGROAL_LIMIT_ENTRY_DATABASE, database) == 0
                && pgagroal_apply_limit_configuration_string(&config->limits[index], PGAGROAL_LIMIT_ENTRY_USERNAME, username) == 0
                && pgagroal_apply_limit_configuration_int(&config->limits[index], PGAGROAL_LIMIT_ENTRY_MAX_SIZE, max_size) == 0
                && pgagroal_apply_limit_configuration_int(&config->limits[index], PGAGROAL_LIMIT_ENTRY_MIN_SIZE, min_size) == 0
                && pgagroal_apply_limit_configuration_int(&config->limits[index], PGAGROAL_LIMIT_ENTRY_LINENO, lineno) == 0
                && pgagroal_apply_limit_configuration_int(&config->limits[index], PGAGROAL_LIMIT_ENTRY_INITIAL_SIZE, initial_size) == 0)
            {
               // configuration applied
               server_max -= max_size;

               memcpy(&(config->limits[index].database), database, strlen(database));
               memcpy(&(config->limits[index].username), username, strlen(username));
               config->limits[index].max_size = max_size;
               config->limits[index].initial_size = initial_size;
               config->limits[index].min_size = min_size;
               config->limits[index].lineno = lineno;
               atomic_init(&config->limits[index].active_connections, 0);

               index++;

               if (index >= NUMBER_OF_LIMITS)
               {
                  warnx("Too many LIMIT entries (max is %d)\n", NUMBER_OF_LIMITS);
                  fclose(file);
                  return PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
               }

            }
            else
            {
               warnx("Invalid LIMIT entry /%s:%d)", config->limit_path, lineno);
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

   return PGAGROAL_CONFIGURATION_STATUS_OK;
}

/**
 *
 */
int
pgagroal_validate_limit_configuration(void* shm)
{
   int total_connections;
   struct main_configuration* config;

   total_connections = 0;
   config = (struct main_configuration*)shm;

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

         if (config->limits[i].initial_size != 0 && config->limits[i].initial_size < config->limits[i].min_size)
         {
            pgagroal_log_warn("initial_size smaller than min_size for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
            pgagroal_log_info("Adjusting initial_size from %d to %d (min_size) for limit entry %d (%s:%d)",
                              config->limits[i].initial_size,
                              config->limits[i].min_size, i + 1, config->limit_path, config->limits[i].lineno);
            config->limits[i].initial_size = config->limits[i].min_size;
         }

         if (config->limits[i].initial_size != 0 && config->limits[i].initial_size > config->limits[i].max_size)
         {
            pgagroal_log_warn("initial_size greater than max_size for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
            pgagroal_log_info("Adjusting initial_size from %d to %d (max_size) for limit entry %d (%s:%d)",
                              config->limits[i].initial_size, config->limits[i].max_size
                              , i + 1, config->limit_path, config->limits[i].lineno);
            config->limits[i].initial_size = config->limits[i].max_size;
         }

         if (config->limits[i].max_size < config->limits[i].min_size)
         {
            pgagroal_log_warn("max_size smaller than min_size for limit entry %d (%s:%d)", i + 1, config->limit_path, config->limits[i].lineno);
            pgagroal_log_info("Adjusting min_size from %d to %d (max_size) for limit entry %d (%s:%d)",
                              config->limits[i].min_size, config->limits[i].max_size
                              , i + 1, config->limit_path, config->limits[i].lineno);
            config->limits[i].min_size = config->limits[i].max_size;
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
   struct main_configuration* config;
   int status;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }
   config = (struct main_configuration*)shm;

   if (pgagroal_get_master_key(&master_key, config->master_key_file_location))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
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

   config->number_of_users = index;

   if (config->number_of_users > NUMBER_OF_USERS)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
      goto error;
   }

   free(master_key);

   fclose(file);

   return PGAGROAL_CONFIGURATION_STATUS_OK;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return status;
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
   struct main_configuration* config;
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }
   config = (struct main_configuration*)shm;

   if (pgagroal_get_master_key(&master_key, config->master_key_file_location))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
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

   config->number_of_frontend_users = index;

   if (config->number_of_frontend_users > NUMBER_OF_USERS)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
      goto error;
   }

   free(master_key);

   fclose(file);

   return PGAGROAL_CONFIGURATION_STATUS_OK;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return status;
}

/**
 *
 */
int
pgagroal_validate_frontend_users_configuration(void* shm)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shm;

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
   struct main_configuration* config;
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }
   config = (struct main_configuration*)shm;

   if (pgagroal_get_master_key(&master_key, config->master_key_file_location))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
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

   config->number_of_admins = index;

   if (config->number_of_admins > NUMBER_OF_ADMINS)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
      goto error;
   }

   free(master_key);

   fclose(file);

   return PGAGROAL_CONFIGURATION_STATUS_OK;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return status;
}

/**
 *
 */
int
pgagroal_vault_read_users_configuration(void* shm, char* filename)
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
   struct vault_configuration* config;
   struct main_configuration* main_config;
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }
   main_config = (struct main_configuration*)shm;

   if (pgagroal_get_master_key(&master_key, main_config->master_key_file_location))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;
   config = (struct vault_configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (strlen(username) < MAX_USERNAME_LENGTH &&
             strlen(password) < MAX_PASSWORD_LENGTH &&
             !strcmp(config->vault_server.user.username, username))
         {
            memcpy(&config->vault_server.user.password, password, strlen(password));
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

   config->number_of_users = index;

   if (config->number_of_users > NUMBER_OF_ADMINS)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
      goto error;
   }

   free(master_key);

   fclose(file);

   return PGAGROAL_CONFIGURATION_STATUS_OK;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return status;
}

/**
 *
 */
int
pgagroal_validate_admins_configuration(void* shm)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shm;

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
   struct main_configuration* config;
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }
   config = (struct main_configuration*)shm;

   if (pgagroal_get_master_key(&master_key, config->master_key_file_location))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         if (index > 0)
         {
            status = PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
            goto error;
         }

         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;
         }

         if (pgagroal_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
            goto error;

         }

         if (pgagroal_decrypt(decoded, decoded_length, master_key, &password))
         {
            status = PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT;
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

   free(master_key);

   fclose(file);

   return PGAGROAL_CONFIGURATION_STATUS_OK;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return status;
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
   struct main_configuration* reload = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_trace("Configuration: %s", config->common.configuration_path);
   pgagroal_log_trace("HBA: %s", config->hba_path);
   pgagroal_log_trace("Limit: %s", config->limit_path);
   pgagroal_log_trace("Users: %s", config->users_path);
   pgagroal_log_trace("Frontend users: %s", config->frontend_users_path);
   pgagroal_log_trace("Admins: %s", config->admins_path);
   pgagroal_log_trace("Superuser: %s", config->superuser_path);

   reload_size = sizeof(struct main_configuration);

   if (pgagroal_create_shared_memory(reload_size, HUGEPAGE_OFF, (void**)&reload))
   {
      goto error;
   }

   pgagroal_init_configuration((void*)reload);

   if (pgagroal_read_configuration((void*)reload, config->common.configuration_path, true))
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

   if (transfer_configuration(config, reload) > 0)
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

/**
 * Given a line of text extracts the key part and the value.
 * Valid lines must have the form <key> = <value>.
 *
 * The key must be unquoted and cannot have any spaces
 * in front of it.
 *
 * Comments on the right side of a value are allowed.
 *
 * The value can be quoted, and this allows for inserting spaces
 * and comment signs. Quotes are '""' and '\''.
 * Example of valid lines are:
 * <code>
 * foo = bar
 * foo=bar
 * foo=  bar
 * foo = "bar"
 * foo = 'bar'
 * foo = "#bar"
 * foo = '#bar'
 * foo = bar # bar set!
 * foo = bar# bar set!
 * </code>
 *
 * @param str the line of text incoming from the configuration file
 * @param key the pointer to where to store the key extracted from the line
 * @param value the pointer to where to store the value (unquoted)
 * @returns 1 if unable to parse the line, 0 if everything is ok
 */
static int
extract_key_value(char* str, char** key, char** value)
{
   int c = 0;
   int offset = 0;
   int length = strlen(str);
   char* k;
   char* v;
   char quoting_begin = '\0';
   char quoting_end = '\0';

   // the key does not allow spaces and is whatever is
   // on the left of the '='
   while (str[c] != ' ' && str[c] != '=' && c < length)
      c++;

   if (c < length)
   {
      k = calloc(1, c + 1);
      if (k == NULL)
      {
         goto error;
      }
      memcpy(k, str, c);
      *key = k;

      while ((str[c] == ' ' || str[c] == '\t' || str[c] == '=') && c < length)
         c++;

      offset = c;

      // the value of the parameter starts from offset 'offset'
      while (str[c] != '\r' && str[c] != '\n' && c < length)
      {
         if (str[c] == '\'' || str[c] == '"')
         {
            if (quoting_begin == '\0')
            {
               quoting_begin = str[c];
               offset = c + 1;    // start at the very first character after the quote
            }
            else if (str[c] == quoting_begin && quoting_end == '\0')
            {
               quoting_end = str[c];
               // end at the last character before the quote
               break;
            }
         }
         else if (str[c] == '#' || str[c] == ';')
         {
            if (quoting_begin == '\0' || (quoting_begin != '\0' && quoting_end != '\0'))
            {
               // a comment outside of quoted string, ignore anything else
               break;
            }
         }
         else if (str[c] == ' ')
         {
            if (quoting_begin == '\0' || (quoting_begin != '\0' && quoting_end != '\0'))
            {
               // space outside a quoted string, stop here
               break;
            }
         }

         c++;
      }

      // quotes must be the same!
      if (quoting_begin != '\0' && quoting_begin != quoting_end)
      {
         goto error;
      }

      if (c <= length)
      {
         v = calloc(1, (c - offset) + 1);
         if (v == NULL)
         {
            goto error;
         }
         memcpy(v, str + offset, (c - offset));
         *value = v;
         return 0;
      }
   }
error:
   return 1;
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
   if (!strcasecmp(str, "true") || !strcasecmp(str, "on") || !strcasecmp(str, "yes") || !strcasecmp(str, "1"))
   {
      *b = true;
      return 0;
   }

   if (!strcasecmp(str, "false") || !strcasecmp(str, "off") || !strcasecmp(str, "no") || !strcasecmp(str, "0"))
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
   {
      return PGAGROAL_LOGGING_TYPE_CONSOLE;
   }

   if (!strcasecmp(str, "file"))
   {
      return PGAGROAL_LOGGING_TYPE_FILE;
   }

   if (!strcasecmp(str, "syslog"))
   {
      return PGAGROAL_LOGGING_TYPE_SYSLOG;
   }

   return PGAGROAL_LOGGING_TYPE_CONSOLE;
}

static int
as_logging_level(char* str)
{
   size_t size = 0;
   int debug_level = 1;
   char* debug_value = NULL;

   if (!strncasecmp(str, "debug", strlen("debug")))
   {
      if (strlen(str) > strlen("debug"))
      {
         size = strlen(str) - strlen("debug");
         debug_value = (char*)calloc(1, size + 1);
         if (debug_value == NULL)
         {
            return PGAGROAL_LOGGING_LEVEL_FATAL;
         }
         memcpy(debug_value, str + 5, size);
         if (as_int(debug_value, &debug_level))
         {
            // cannot parse, set it to 1
            debug_level = 1;
         }
         free(debug_value);
      }

      if (debug_level <= 1)
      {
         return PGAGROAL_LOGGING_LEVEL_DEBUG1;
      }
      else if (debug_level == 2)
      {
         return PGAGROAL_LOGGING_LEVEL_DEBUG2;
      }
      else if (debug_level == 3)
      {
         return PGAGROAL_LOGGING_LEVEL_DEBUG3;
      }
      else if (debug_level == 4)
      {
         return PGAGROAL_LOGGING_LEVEL_DEBUG4;
      }
      else if (debug_level >= 5)
      {
         return PGAGROAL_LOGGING_LEVEL_DEBUG5;
      }
   }

   if (!strcasecmp(str, "info"))
   {
      return PGAGROAL_LOGGING_LEVEL_INFO;
   }

   if (!strcasecmp(str, "warn"))
   {
      return PGAGROAL_LOGGING_LEVEL_WARN;
   }

   if (!strcasecmp(str, "error"))
   {
      return PGAGROAL_LOGGING_LEVEL_ERROR;
   }

   if (!strcasecmp(str, "fatal"))
   {
      return PGAGROAL_LOGGING_LEVEL_FATAL;
   }

   return PGAGROAL_LOGGING_LEVEL_INFO;
}

static int
as_logging_mode(char* str)
{
   if (!strcasecmp(str, "a") || !strcasecmp(str, "append"))
   {
      return PGAGROAL_LOGGING_MODE_APPEND;
   }

   if (!strcasecmp(str, "c") || !strcasecmp(str, "create"))
   {
      return PGAGROAL_LOGGING_MODE_CREATE;
   }

   return PGAGROAL_LOGGING_MODE_APPEND;
}

static int
as_validation(char* str)
{
   if (!strcasecmp(str, "off"))
   {
      return VALIDATION_OFF;
   }

   if (!strcasecmp(str, "foreground"))
   {
      return VALIDATION_FOREGROUND;
   }

   if (!strcasecmp(str, "background"))
   {
      return VALIDATION_BACKGROUND;
   }

   return VALIDATION_OFF;
}

static int
as_pipeline(char* str)
{
   if (!strcasecmp(str, "auto"))
   {
      return PIPELINE_AUTO;
   }

   if (!strcasecmp(str, "performance"))
   {
      return PIPELINE_PERFORMANCE;
   }

   if (!strcasecmp(str, "session"))
   {
      return PIPELINE_SESSION;
   }

   if (!strcasecmp(str, "transaction"))
   {
      return PIPELINE_TRANSACTION;
   }

   return PIPELINE_AUTO;
}

static int
as_hugepage(char* str)
{
   if (!strcasecmp(str, "off"))
   {
      return HUGEPAGE_OFF;
   }

   if (!strcasecmp(str, "try"))
   {
      return HUGEPAGE_TRY;
   }

   if (!strcasecmp(str, "on"))
   {
      return HUGEPAGE_ON;
   }

   return HUGEPAGE_OFF;
}

static void
extract_hba(char* str, char** type, char** database, char** user, char** address, char** method)
{
   int offset = 0;
   int length = strlen(str);

   offset = extract_value(str, offset, type);

   if (offset == -1 || offset >= length)
   {
      return;
   }

   offset = extract_value(str, offset, database);

   if (offset == -1 || offset >= length)
   {
      return;
   }

   offset = extract_value(str, offset, user);

   if (offset == -1 || offset >= length)
   {
      return;
   }

   offset = extract_value(str, offset, address);

   if (offset == -1 || offset >= length)
   {
      return;
   }

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
   {
      return;
   }

   offset = extract_value(str, offset, user);

   if (offset == -1 || offset >= length)
   {
      return;
   }

   offset = extract_value(str, offset, &value);

   if (offset == -1)
   {
      return;
   }

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
   {
      return;
   }

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
            *initial_size = 0;
            return;
         }
      }
   }

   free(value);
   value = NULL;

   offset = extract_value(str, offset, &value);

   if (offset == -1)
   {
      return;
   }

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
            *min_size = 0;
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

      if (offset <= length)
      {
         to = offset;

         v = calloc(1, to - from + 1);
         if (v == NULL)
         {
            return -1;
         }
         memcpy(v, str + from, to - from);
         *value = v;

         return offset;
      }
   }

   return -1;
}

/**
 * Utility function to copy all the settings from the source configuration
 * to the destination one. This is useful for example when a reload
 * command is issued.
 *
 * @param config the new (clean) configuration
 * @param reload the one loaded from the configuration (i.e., the one to apply)
 * @return 0 on success, a negative number in the case some parameters cannot be changed
 * because require a restart (in such case, the value indicates the number of untouched
 * parameters), a positive value in the case of a dramatic error.
 */
static int
transfer_configuration(struct main_configuration* config, struct main_configuration* reload)
{
#ifdef HAVE_LINUX
   sd_notify(0, "RELOADING=1");
#endif

   int unchanged = 0;

   memcpy(config->common.host, reload->common.host, MISC_LENGTH);
   config->common.port = reload->common.port;
   config->metrics = reload->metrics;
   config->metrics_cache_max_age = reload->metrics_cache_max_age;
   unchanged -= restart_int("metrics_cache_max_size", config->metrics_cache_max_size, reload->metrics_cache_max_size);
   config->management = reload->management;

   config->update_process_title = reload->update_process_title;

   /* gracefully */

   /* disabled */

   /* pipeline */
   unchanged -= restart_int("pipeline", config->pipeline, reload->pipeline);

   config->failover = reload->failover;
   memcpy(config->failover_script, reload->failover_script, MISC_LENGTH);

   /* log_type */
   restart_int("log_type", config->common.log_type, reload->common.log_type);
   config->common.log_level = reload->common.log_level;

   /* log_path */
   // if the log main parameters have changed, we need
   // to restart the logging system
   if (strncmp(config->common.log_path, reload->common.log_path, MISC_LENGTH)
       || config->common.log_rotation_size != reload->common.log_rotation_size
       || config->common.log_rotation_age != reload->common.log_rotation_age
       || config->common.log_mode != reload->common.log_mode)
   {
      pgagroal_log_debug("Log restart triggered!");
      pgagroal_stop_logging();
      config->common.log_rotation_size = reload->common.log_rotation_size;
      config->common.log_rotation_age = reload->common.log_rotation_age;
      config->common.log_mode = reload->common.log_mode;
      memcpy(config->common.log_line_prefix, reload->common.log_line_prefix, MISC_LENGTH);
      memcpy(config->common.log_path, reload->common.log_path, MISC_LENGTH);
      pgagroal_start_logging();
   }

   config->common.log_connections = reload->common.log_connections;
   config->common.log_disconnections = reload->common.log_disconnections;

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
   unchanged -= restart_int("max_connections", config->max_connections, reload->max_connections);
   config->allow_unknown_users = reload->allow_unknown_users;

   config->blocking_timeout = reload->blocking_timeout;
   config->idle_timeout = reload->idle_timeout;
   config->rotate_frontend_password_timeout = reload->rotate_frontend_password_timeout;
   config->rotate_frontend_password_length = reload->rotate_frontend_password_length;
   config->max_connection_age = reload->max_connection_age;
   config->validation = reload->validation;
   config->background_interval = reload->background_interval;
   config->max_retries = reload->max_retries;
   config->authentication_timeout = reload->authentication_timeout;
   config->disconnect_client = reload->disconnect_client;
   config->disconnect_client_force = reload->disconnect_client_force;
   /* pidfile */
   restart_string("pidfile", config->pidfile, reload->pidfile, true);

   /* libev */
   restart_string("libev", config->libev, reload->libev, true);
   config->buffer_size = reload->buffer_size;
   config->keep_alive = reload->keep_alive;
   config->nodelay = reload->nodelay;
   config->non_blocking = reload->non_blocking;
   config->backlog = reload->backlog;
   /* hugepage */
   unchanged -= restart_int("hugepage", config->hugepage, reload->hugepage);
   config->tracker = reload->tracker;
   config->track_prepared_statements = reload->track_prepared_statements;

   /* unix_socket_dir */

   // does make sense to check for remote connections? Because in the case the Unix socket dir
   // changes the pgagroal-cli probably will not be able to connect in any case!
   restart_string("unix_socket_dir", config->unix_socket_dir, reload->unix_socket_dir, false);

   /* su_connection */

   /* states */

   // decreasing the number of servers is probably a bad idea
   if (config->number_of_servers > reload->number_of_servers)
   {
      restart_int("decreasing number of servers", config->number_of_servers, reload->number_of_servers);
   }

   for (int i = 0; i < reload->number_of_servers; i++)
   {
      // check and emit restart warning only for not-added servers
      if (i < config->number_of_servers)
      {
         restart_server(&reload->servers[i], &config->servers[i]);
      }

      copy_server(&config->servers[i], &reload->servers[i]);
   }
   config->number_of_servers = reload->number_of_servers;

   // zero fill remaining memory that is unused
   memset(&config->servers[config->number_of_servers], 0,
          sizeof(struct server) * (NUMBER_OF_SERVERS - config->number_of_servers));

   memset(&config->hbas[0], 0, sizeof(struct hba) * NUMBER_OF_HBAS);
   for (int i = 0; i < reload->number_of_hbas; i++)
   {
      copy_hba(&config->hbas[i], &reload->hbas[i]);
   }
   config->number_of_hbas = reload->number_of_hbas;

   /* number_of_limits */
   /* limits */
   unchanged -= restart_limit("limits", config, reload);

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

   if (unchanged < 0)
   {
      pgagroal_log_warn("%d settings cannot be applied", unchanged * -1);
   }

   return unchanged;
}

/**
 * Checks if the configuration of the first server
 * is the same as the configuration of the second server.
 * So far it tests for the same connection string, meaning
 * that the hostname and the port must be the same (i.e.,
 * pointing to the same endpoint).
 * It does not resolve the hostname, therefore 'localhost' and '127.0.0.1'
 * are considered as different hosts.
 * @return true if the server configurations look the same
 */
static bool
is_same_server(struct server* s1, struct server* s2)
{
   if (!strncmp(s1->host, s2->host, MISC_LENGTH) && s1->port == s2->port)
   {
      return true;
   }
   else
   {
      return false;
   }
}

/**
 * Checks if TLS configurations are same.
 * @return true if the TLS configurations are same
 */
static bool
is_same_tls(struct server* src, struct server* dst)
{
   if (src->tls == dst->tls &&
       !strncmp(src->tls_cert_file, dst->tls_cert_file, MISC_LENGTH) &&
       !strncmp(src->tls_key_file, dst->tls_key_file, MISC_LENGTH) &&
       !strncmp(src->tls_ca_file, dst->tls_ca_file, MISC_LENGTH))
   {
      return true;
   }
   else
   {
      return false;
   }
}

static void
copy_server(struct server* dst, struct server* src)
{
   atomic_schar state;

   // check the server cloned "seems" the same
   if (is_same_server(dst, src))
   {
      state = atomic_load(&dst->state);
   }
   else
   {
      state = SERVER_NOTINIT;
   }

   memset(dst, 0, sizeof(struct server));
   memcpy(&dst->name[0], &src->name[0], MISC_LENGTH);
   memcpy(&dst->host[0], &src->host[0], MISC_LENGTH);
   dst->port = src->port;
   atomic_init(&dst->state, state);
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

/**
 * Utility function prints a line in the log when a restart is required.
 * @return 0 when parameter values are same, 1 when a restart required.
 */
static int
restart_bool(char* name, bool e, bool n)
{
   if (e != n)
   {
      pgagroal_log_info("Restart required for %s - Existing %s New %s", name, e ? "true" : "false", n ? "true" : "false");
      return 1;
   }

   return 0;
}

/**
 * Utility function to notify when a string parameter in the
 * configuration requires a restart.
 * Prints a line in the log when a restart is required.
 *
 * @param name the name of the parameter
 * @param e the existing (current) value of the parameter
 * @param n the new value
 * @param skip_non_existing if true it will ignore when 'n' is empty,
 * used when the parameter is automatically set
 * @return 0 when the parameter values are the same, 1 when it is required
 * a restart
 */
static int
restart_string(char* name, char* e, char* n, bool skip_non_existing)
{
   if (skip_non_existing && !strlen(n))
   {
      return 0;
   }

   if (strcmp(e, n))
   {
      pgagroal_log_info("Restart required for %s - Existing %s New %s", name, e, n);
      return 1;
   }

   return 0;
}

static int
restart_limit(char* name, struct main_configuration* config, struct main_configuration* reload)
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

static int
restart_server(struct server* src, struct server* dst)
{
   char restart_message[2 * MISC_LENGTH];

   if (!is_same_server(src, dst))
   {
      snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <host>", src->name);
      restart_string(restart_message, dst->host, src->host, false);
      snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <port>", src->name);
      restart_int(restart_message, dst->port, src->port);
      return 1;
   }
   else if (!is_same_tls(src, dst))
   {
      snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <tls>", src->name);
      restart_bool(restart_message, dst->tls, src->tls);
      snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <tls_cert_file>", src->name);
      restart_string(restart_message, dst->tls_cert_file, src->tls_cert_file, false);
      snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <tls_key_file>", src->name);
      restart_string(restart_message, dst->tls_key_file, src->tls_key_file, false);
      snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <tls_ca_file>", src->name);
      restart_string(restart_message, dst->tls_ca_file, src->tls_ca_file, false);
      return 1;
   }

   return 0;
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
 */
static int
as_logging_rotation_size(char* str, unsigned int* size)
{
   return as_bytes(str, size, PGAGROAL_LOGGING_ROTATION_DISABLED);
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
 *
 */
static int
as_logging_rotation_age(char* str, unsigned int* age)
{
   return as_seconds(str, age, PGAGROAL_LOGGING_ROTATION_DISABLED);
}

void
pgagroal_init_pidfile_if_needed(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) == 0)
   {
      // no pidfile set, use a default one
      snprintf(config->pidfile, sizeof(config->pidfile), "%s/pgagroal.%d.pid",
               config->unix_socket_dir,
               config->common.port);
      pgagroal_log_debug("PID file automatically set to: [%s]", config->pidfile);
   }
}

bool
pgagroal_can_prefill(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->number_of_users > 0 && config->number_of_limits > 0)
   {
      return true;
   }
   else
   {
      return false;
   }
}

/**
 * Function to check if the specified key belongs to the right section.
 * The idea is to pass all the values read from the configuration file,
 * and a boolean parameter to check if the section the parameter belongs is global or not.
 * A global section is the main `pgagroal` section, while a local section
 * is a custom user section, i.e., a server section.
 *
 * @param wanted the key we want to match against
 * @param section the section in which the key has been found
 * @param key the key read from the configuration file
 * @param global true if the `section` has to be `pgagroal`
 * @param unknown set to true if the key does match but the section does not.
 *        For instance the key `host` found in a local section while required
 *        to be global will set `unknown` to true.
 *        This parameter can be omitted.
 *
 * @returns true if the key matches and the section is of the specified type.
 *
 * Example:
 *  key_in_section("host", section, key, true, &unknown); // search for [pgagroal] -> host
 *  key_in_section("port", section, key, false, &unknown); // search for server section -> port
 */
static bool
key_in_section(char* wanted, char* section, char* key, bool global, bool* unknown)
{

   // first of all, look for a key match
   if (strncmp(wanted, key, MISC_LENGTH))
   {
      // no match at all
      return false;
   }

   // if here there is a match on the key, ensure the section is
   // appropriate
   if (global && (!strncmp(section, PGAGROAL_MAIN_INI_SECTION, MISC_LENGTH) | !strncmp(section, PGAGROAL_VAULT_INI_SECTION, MISC_LENGTH)))
   {
      return true;
   }
   else if (!global && strlen(section) > 0)
   {
      return true;
   }
   else
   {
      if (unknown)
      {
         *unknown = true;
      }

      return false;
   }
}

/**
 * Function to see if the specified line is a comment line
 * and has to be ignored.
 * A comment line is a line that starts with '#' or ';' or
 * with spaces (or tabs) and a comment sign.
 *
 * @param line the line read from the file
 * @return true if the line is a full comment line
 */
static bool
is_comment_line(char* line)
{
   int c = 0;
   int length = strlen(line);

   while (c < length)
   {
      if (line[c] == '#' || line[c] == ';')
      {
         return true;
      }
      else if (line[c] != ' ' && line[c] != '\t')
      {
         break;
      }

      c++;
   }

   return false;
}

/**
 * Function to inspect a configuration line and detect if it handles a section.
 * If the line handles a section name, like `[pgagroal]` the function does set
 * the `section` argument, otherwise it does nothing.
 *
 * @param line the line to inspect
 * @param section the pointer to the string that will contain
 *        the section name, only if the line handles a section, otherwise
 *        the pointer will not be changed.
 *
 * @returns true if the line handles a section and the `section` pointer
 *          has been changed
 */
static bool
section_line(char* line, char* section)
{
   size_t max;
   char* ptr = NULL;

   // if does not appear to be a section line do nothing!
   if (line[0] != '[')
   {
      return false;
   }

   ptr = strchr(line, ']');
   if (ptr)
   {
      memset(section, 0, LINE_LENGTH);
      max = ptr - line - 1;
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(section, line + 1, max);
      return true;
   }

   return false;

}

/**
 * Parses an age string, providing the resulting value as seconds.
 * An age string is expressed by a number and a suffix that indicates
 * the multiplier. Accepted suffixes, case insensitive, are:
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in seconds.
 *
 * @param str the value to parse as retrieved from the configuration
 * @param age a pointer to the value that is going to store
 *        the resulting number of seconds
 * @param default_age a value to set when the parsing is unsuccesful

 */
static unsigned int
as_seconds(char* str, unsigned int* age, unsigned int default_age)
{
   int multiplier = 1;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   int i_value = default_age;

   if (is_empty_string(str))
   {
      *age = default_age;
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
         // another extra char not allowed
         goto error;
      }
      else if (isalpha(str[i]) && !multiplier_set)
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
      {
         // do not allow alien chars
         goto error;
      }
   }

   value[index] = '\0';
   if (!as_int(value, &i_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (i_value >= 0)
      {
         *age = i_value * multiplier;
      }
      else
      {
         goto error;
      }

      return 0;
   }
   else
   {
error:
      *age = default_age;
      return 1;
   }
}

/**
 * Converts a "size string" into the number of bytes.
 *
 * Valid strings have one of the suffixes:
 * - b for bytes (default)
 * - k for kilobytes
 * - m for megabytes
 * - g for gigabytes
 *
 * The default is expressed always as bytes.
 * Uppercase letters work too.
 * If no suffix is specified, the value is expressed as bytes.
 *
 * @param str the string to parse (e.g., "2M")
 * @param bytes the value to set as result of the parsing stage
 * @param default_bytes the default value to set when the parsing cannot proceed
 * @return 1 if parsing is unable to understand the string, 0 is parsing is
 *         performed correctly (or almost correctly, e.g., empty string)
 */
static unsigned int
as_bytes(char* str, unsigned int* bytes, unsigned int default_bytes)
{
   int multiplier = 1;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   int i_value = default_bytes;

   if (is_empty_string(str))
   {
      *bytes = default_bytes;
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
         // allow a 'B' suffix on a multiplier
         // like for instance 'MB', but don't allow it
         // for bytes themselves ('BB')
         if (multiplier == 1
             || (str[i] != 'b' && str[i] != 'B'))
         {
            // another non-digit char not allowed
            goto error;
         }
      }
      else if (isalpha(str[i]) && !multiplier_set)
      {
         if (str[i] == 'M' || str[i] == 'm')
         {
            multiplier = 1024 * 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'G' || str[i] == 'g')
         {
            multiplier = 1024 * 1024 * 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'K' || str[i] == 'k')
         {
            multiplier = 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'B' || str[i] == 'b')
         {
            multiplier = 1;
            multiplier_set = true;
         }
      }
      else
      {
         // do not allow alien chars
         goto error;
      }
   }

   value[index] = '\0';
   if (!as_int(value, &i_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (i_value >= 0)
      {
         *bytes = i_value * multiplier;
      }
      else
      {
         goto error;
      }

      return 0;
   }
   else
   {
error:
      *bytes = default_bytes;
      return 1;
   }
}

/**
 * Utility function to understand the setting for updating
 * the process title.
 *
 * @param str the value obtained by the configuration parsing
 * @param policy the pointer to the value where the setting will be stored
 * @param default_policy a value to set when the configuration cannot be
 * understood
 *
 * @return 0 on success, 1 on error. In any case the `policy` variable is set to
 * `default_policy`.
 */
static unsigned int
as_update_process_title(char* str, unsigned int* policy, unsigned int default_policy)
{
   if (is_empty_string(str))
   {
      *policy = default_policy;
      return 1;
   }

   if (!strncmp(str, "never", MISC_LENGTH) || !strncmp(str, "off", MISC_LENGTH))
   {
      *policy = UPDATE_PROCESS_TITLE_NEVER;
      return 0;
   }
   else if (!strncmp(str, "strict", MISC_LENGTH))
   {
      *policy = UPDATE_PROCESS_TITLE_STRICT;
      return 0;
   }
   else if (!strncmp(str, "minimal", MISC_LENGTH))
   {
      *policy = UPDATE_PROCESS_TITLE_MINIMAL;
      return 0;
   }
   else if (!strncmp(str, "verbose", MISC_LENGTH) || !strncmp(str, "full", MISC_LENGTH))
   {
      *policy = UPDATE_PROCESS_TITLE_VERBOSE;
      return 0;
   }
   else
   {
      // not a valid setting
      *policy = default_policy;
      return 1;
   }

}

int
pgagroal_write_config_value(char* buffer, char* config_key, size_t buffer_size)
{
   struct main_configuration* config;

   char section[MISC_LENGTH];
   char context[MISC_LENGTH];
   char key[MISC_LENGTH];
   int begin = -1, end = -1;
   bool main_section;

   config = (struct main_configuration*)shmem;

   memset(section, 0, MISC_LENGTH);
   memset(context, 0, MISC_LENGTH);
   memset(key, 0, MISC_LENGTH);

   for (int i = 0; i < strlen(config_key); i++)
   {
      if (config_key[i] == '.')
      {
         if (!strlen(section))
         {
            memcpy(section, &config_key[begin], end - begin + 1);
            section[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }
         else if (!strlen(context))
         {
            memcpy(context, &config_key[begin], end - begin + 1);
            context[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }
         else if (!strlen(key))
         {
            memcpy(key, &config_key[begin], end - begin + 1);
            key[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }

      }

      if (begin < 0)
      {
         begin = i;
      }

      end = i;

   }

   // if the key has not been found, since there is no ending dot,
   // try to extract it from the string
   if (!strlen(key))
   {
      memcpy(key, &config_key[begin], end - begin + 1);
      key[end - begin + 1] = '\0';
   }

   // force the main section, i.e., global parameters, if and only if
   // there is no section or section is 'pgagroal' without any subsection
   main_section = (!strlen(section) || !strncmp(section, "pgagroal", MISC_LENGTH))
                  && !strlen(context);

   if (!strncmp(section, "server", MISC_LENGTH))
   {
      return pgagroal_write_server_config_value(buffer, context, key, buffer_size);
   }
   else if (!strncmp(section, "hba", MISC_LENGTH))
   {
      return pgagroal_write_hba_config_value(buffer, context, key, buffer_size);
   }
   else if (!strncmp(section, "limit", MISC_LENGTH))
   {
      return pgagroal_write_limit_config_value(buffer, context, key, buffer_size);
   }
   else if (main_section)
   {

      /* global configuration settings */

      if (!strncmp(key, "host", MISC_LENGTH))
      {
         return to_string(buffer, config->common.host, buffer_size);
      }
      else if (!strncmp(key, "port", MISC_LENGTH))
      {
         return to_int(buffer, config->common.port);
      }
      else if (!strncmp(key, "log_type", MISC_LENGTH))
      {
         return to_log_type(buffer, config->common.log_type);
      }
      else if (!strncmp(key, "log_mode", MISC_LENGTH))
      {
         return to_log_mode(buffer, config->common.log_mode);
      }
      else if (!strncmp(key, "log_line_prefix", MISC_LENGTH))
      {
         return to_string(buffer, config->common.log_line_prefix, buffer_size);
      }

      else if (!strncmp(key, "log_level", MISC_LENGTH))
      {
         return to_log_level(buffer, config->common.log_level);
      }
      else if (!strncmp(key, "log_rotation_size", MISC_LENGTH))
      {
         return to_int(buffer, config->common.log_rotation_size);

      }
      else if (!strncmp(key, "log_rotation_age", MISC_LENGTH))
      {
         return to_int(buffer, config->common.log_rotation_age);

      }
      else if (!strncmp(key, "log_connections", MISC_LENGTH))
      {
         return to_bool(buffer, config->common.log_connections);
      }
      else if (!strncmp(key, "log_disconnections", MISC_LENGTH))
      {
         return to_bool(buffer, config->common.log_disconnections);
      }
      else if (!strncmp(key, "log_path", MISC_LENGTH))
      {
         return to_string(buffer, config->common.log_path, buffer_size);
      }
      else if (!strncmp(key, "metrics", MISC_LENGTH))
      {
         return to_int(buffer, config->metrics);
      }
      else if (!strncmp(key, "metrics_cache_max_age", MISC_LENGTH))
      {
         return to_int(buffer, config->metrics_cache_max_age);
      }
      else if (!strncmp(key, "metrics_cache_max_size", MISC_LENGTH))
      {
         return to_int(buffer, config->metrics_cache_max_size);
      }
      else if (!strncmp(key, "management", MISC_LENGTH))
      {
         return to_int(buffer, config->management);
      }
      else if (!strncmp(key, "pipeline", MISC_LENGTH))
      {
         return to_pipeline(buffer, config->pipeline);
      }
      else if (!strncmp(key, "failover_script", MISC_LENGTH))
      {
         return to_string(buffer, config->failover_script, buffer_size);
      }
      else if (!strncmp(key, "tls", MISC_LENGTH))
      {
         return to_bool(buffer, config->tls);
      }
      else if (!strncmp(key, "auth_query", MISC_LENGTH))
      {
         return to_bool(buffer, config->authquery);
      }
      else if (!strncmp(key, "tls_ca_file", MISC_LENGTH))
      {
         return to_string(buffer, config->tls_ca_file, buffer_size);
      }
      else if (!strncmp(key, "tls_cert_file", MISC_LENGTH))
      {
         return to_string(buffer, config->tls_cert_file, buffer_size);
      }
      else if (!strncmp(key, "tls_key_file", MISC_LENGTH))
      {
         return to_string(buffer, config->tls_key_file, buffer_size);
      }
      else if (!strncmp(key, "blocking_timeout", MISC_LENGTH))
      {
         return to_int(buffer, config->blocking_timeout);
      }
      else if (!strncmp(key, "idle_timeout", MISC_LENGTH))
      {
         return to_int(buffer, config->idle_timeout);
      }
      else if (!strncmp(key, "rotate_frontend_password_timeout", MISC_LENGTH))
      {
         return to_int(buffer, config->rotate_frontend_password_timeout);
      }
      else if (!strncmp(key, "rotate_frontend_password_length", MISC_LENGTH))
      {
         return to_int(buffer, config->rotate_frontend_password_length);
      }
      else if (!strncmp(key, "max_connection_age", MISC_LENGTH))
      {
         return to_int(buffer, config->max_connection_age);
      }
      else if (!strncmp(key, "validation", MISC_LENGTH))
      {
         return to_validation(buffer, config->validation);
      }
      else if (!strncmp(key, "update_process_title", MISC_LENGTH))
      {
         return to_update_process_title(buffer, config->update_process_title);
      }
      else if (!strncmp(key, "background_interval", MISC_LENGTH))
      {
         return to_int(buffer, config->background_interval);
      }
      else if (!strncmp(key, "max_retries", MISC_LENGTH))
      {
         return to_int(buffer, config->max_retries);
      }
      else if (!strncmp(key, "authentication_timeout", MISC_LENGTH))
      {
         return to_int(buffer, config->authentication_timeout);
      }
      else if (!strncmp(key, "disconnect_client", MISC_LENGTH))
      {
         return to_int(buffer, config->disconnect_client);
      }
      else if (!strncmp(key, "pidfile", MISC_LENGTH))
      {
         return to_string(buffer, config->pidfile, buffer_size);
      }
      else if (!strncmp(key, "allow_unknown_users", MISC_LENGTH))
      {
         return to_bool(buffer, config->allow_unknown_users);
      }
      else if (!strncmp(key, "max_connections", MISC_LENGTH))
      {
         return to_int(buffer, config->max_connections);
      }
      else if (!strncmp(key, "unix_socket_dir", MISC_LENGTH))
      {
         return to_string(buffer, config->unix_socket_dir, buffer_size);
      }
      else if (!strncmp(key, "buffer_size", MISC_LENGTH))
      {
         return to_int(buffer, config->buffer_size);
      }
      else if (!strncmp(key, "keep_alive", MISC_LENGTH))
      {
         return to_bool(buffer, config->keep_alive);
      }
      else if (!strncmp(key, "nodelay", MISC_LENGTH))
      {
         return to_int(buffer, config->nodelay);
      }
      else if (!strncmp(key, "non_blocking", MISC_LENGTH))
      {
         return to_bool(buffer, config->non_blocking);
      }
      else if (!strncmp(key, "backlog", MISC_LENGTH))
      {
         return to_int(buffer, config->backlog);
      }
      else if (!strncmp(key, "hugepage", MISC_LENGTH))
      {
         return to_bool(buffer, config->hugepage);
      }
      else if (!strncmp(key, "track_prepared_statements", MISC_LENGTH))
      {
         return to_bool(buffer, config->track_prepared_statements);
      }
      else
      {
         goto error;
      }

   }    // end of global configuration settings
   else
   {
      goto error;
   }

   return 0;
error:
   pgagroal_log_debug("Unknown configuration key <%s>", config_key);
   return 1;

}

/**
 * Function to extract a configuration value for a specific server.
 * @param server_name the name of the server
 * @param config_key one of the configuration keys allowed in the server section
 * @param buffer the buffer where to write the stringified version of the value
 * @param buffer_size the max size of the buffer where the result will be stored
 * @return 0 on success
 */
static int
pgagroal_write_server_config_value(char* buffer, char* server_name, char* config_key, size_t buffer_size)
{
   int server_index = -1;
   struct main_configuration* config;
   int state;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      if (!strncmp(config->servers[i].name, server_name, MISC_LENGTH))
      {
         /* this is the right server */
         server_index = i;
         break;
      }
   }

   if (server_index < 0 || server_index > NUMBER_OF_SERVERS)
   {
      pgagroal_log_debug("Unable to find a server named <%s> in the current configuration", server_name);
      goto error;
   }

   if (!strncmp(config_key, "host", MISC_LENGTH))
   {
      return to_string(buffer, config->servers[server_index].host, buffer_size);
   }
   else if (!strncmp(config_key, "port", MISC_LENGTH))
   {
      return to_int(buffer, config->servers[server_index].port);
   }
   else if (!strncmp(config_key, "primary", MISC_LENGTH))
   {
      state = atomic_load(&config->servers[server_index].state);
      bool primary = false;
      switch (state)
      {
         case SERVER_NOTINIT_PRIMARY:
         case SERVER_PRIMARY:
            primary = true;
            break;
         default:
            primary = false;

      }

      return to_bool(buffer, primary);
   }
   else if (!strncmp(config_key, "tls", MISC_LENGTH))
   {
      return to_bool(buffer, config->servers[server_index].tls);
   }
   else if (!strncmp(config_key, "tls_cert_file", MISC_LENGTH))
   {
      return to_string(buffer, config->servers[server_index].tls_cert_file, buffer_size);
   }
   else if (!strncmp(config_key, "tls_key_file", MISC_LENGTH))
   {
      return to_string(buffer, config->servers[server_index].tls_key_file, buffer_size);
   }
   else if (!strncmp(config_key, "tls_ca_file", MISC_LENGTH))
   {
      return to_string(buffer, config->servers[server_index].tls_ca_file, buffer_size);
   }
   else
   {
      goto error;
   }

error:
   return 1;
}

/**
 * Method to extract a configuration value for an HBA entry.
 *
 * Please note that seeking for a username does not provide all the
 * available configurations, since the same username could have been
 * listed multiple times. Only the first match is returned.
 *
 * @param buffer where to write the stringified value
 * @param username the username that must match the entry on the HBA entry line
 * @param config_key the configuration parameter to search for
 * @param buffer_size the max length of the destination buffer
 * @return 0 on success
 */
static int
pgagroal_write_hba_config_value(char* buffer, char* username, char* config_key, size_t buffer_size)
{
   int hba_index = -1;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_HBAS; i++)
   {
      if (!strncmp(config->hbas[i].username, username, MISC_LENGTH))
      {
         /* this is the right hba entry */
         hba_index = i;
         break;
      }
   }

   if (hba_index < 0 || hba_index > NUMBER_OF_HBAS)
   {
      pgagroal_log_warn("Unable to find a user named <%s> in the current configuration", username);
      goto error;
   }

   if (!strncmp(config_key, "type", MISC_LENGTH))
   {
      return to_string(buffer, config->hbas[hba_index].type, buffer_size);
   }
   else if (!strncmp(config_key, "database", MISC_LENGTH))
   {
      return to_string(buffer, config->hbas[hba_index].database, buffer_size);
   }
   else if (!strncmp(config_key, "username", MISC_LENGTH))
   {
      return to_string(buffer, config->hbas[hba_index].username, buffer_size);
   }
   else if (!strncmp(config_key, "address", MISC_LENGTH))
   {
      return to_string(buffer, config->hbas[hba_index].address, buffer_size);
   }
   else if (!strncmp(config_key, "method", MISC_LENGTH))
   {
      return to_string(buffer, config->hbas[hba_index].method, buffer_size);
   }
   else
   {
      goto error;
   }

error:
   return 1;
}

/**
 * Given a specific username, retrieves the informations about the limit
 * configuration. The limit configuration is matched against a specific
 * database.
 *
 * @param buffer where to write the information
 * @param database the username to search for
 * @param config_key the value to seek into the limits
 * @param buffer_size the max size of the destination buffer where the result will be written
 * @return 0 on success
 */
static int
pgagroal_write_limit_config_value(char* buffer, char* database, char* config_key, size_t buffer_size)
{
   int limit_index = -1;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_LIMITS; i++)
   {
      if (!strncmp(config->limits[i].database, database, MISC_LENGTH))
      {
         /* this is the right database entry */
         limit_index = i;
         break;
      }
   }

   if (limit_index < 0 || limit_index > NUMBER_OF_LIMITS)
   {
      pgagroal_log_warn("Unable to find a database named <%s> in the current limit configuration", database);
      goto error;
   }

   if (!strncmp(config_key, "username", MISC_LENGTH))
   {
      return to_string(buffer, config->limits[limit_index].username, buffer_size);
   }
   else if (!strncmp(config_key, "database", MISC_LENGTH))
   {
      return to_string(buffer, config->limits[limit_index].database, buffer_size);
   }
   else if (!strncmp(config_key, "max_size", MISC_LENGTH))
   {
      return to_int(buffer, config->limits[limit_index].max_size);
   }
   else if (!strncmp(config_key, "min_size", MISC_LENGTH))
   {
      return to_int(buffer, config->limits[limit_index].min_size);
   }
   else if (!strncmp(config_key, "initial_size", MISC_LENGTH))
   {
      return to_int(buffer, config->limits[limit_index].initial_size);
   }
   else
   {
      goto error;
   }

error:
   return 1;
}

/**
 * An utility function to place an integer value into a string.
 * @param where the string where to print the value, must be already allocated
 * @param value the value to convert into a string
 * @return 0 on success, 1 otherwise
 */
static int
to_int(char* where, int value)
{
   if (!where)
   {
      return 1;
   }

   snprintf(where, MISC_LENGTH, "%d", value);
   return 0;
}

/**
 * An utility function to place a boolean value into a string.
 * The value is always converted in either "on" or "off".
 *
 * @param where the string where to print the value, must be already allocated
 * @param value the value to convert into a string
 * @return 0 on success, 1 otherwise
 */
static int
to_bool(char* where, bool value)
{
   if (!where)
   {
      return 1;
   }

   snprintf(where, MISC_LENGTH, "%s", value ? "on" : "off");
   return 0;
}

/**
 * An utility function to place a string into another string.
 *
 * In the case the string has inner spaces, such spaces are quoted. The function
 * tries to be as smart as possible identifying if there is the need for
 * single or double quotes.
 *
 * The function accepts the size of the destination string, and before writing
 * into such a string the result, it zero fills it. This means it is not mandatory
 * to zero fill the destination string before calling this function.
 * Also please note that if the string that is copied into the destination string
 * has a length bigger than that specified, the function will not copy any data
 * (and will not zero set the destination string, that will remain untouched!)
 *
 * @param where the string where to print the value, must be already allocated
 * @param value the value to convert into a string
 * @param max_length the max length of the 'where' destination string
 * @return 0 on success, 1 otherwise
 */
static int
to_string(char* where, char* value, size_t max_length)
{
   bool needs_quotes = false;
   bool has_double_quotes = false;
   bool has_single_quotes = false;
   char quoting_char = '\0';
   int index = 0;

   if (!where || !value || strlen(value) >= max_length)
   {
      return 1;
   }

   // assume strings with spaces must be quoted
   for (int i = 0; i < strlen(value); i++)
   {
      if (value[i] == ' ')
      {
         needs_quotes = true;
      }
      else if (value[i] == '"')
      {
         has_double_quotes = true;
      }
      else if (value[i] == '\'')
      {
         has_single_quotes = true;
      }

   }

   needs_quotes = needs_quotes || has_double_quotes || has_single_quotes;

   if (needs_quotes)
   {
      // there must be space for quotes
      if (strlen(value) > max_length - 2 - 1)
      {
         return 1;
      }

      if (!has_single_quotes)
      {
         quoting_char = '\'';
      }
      else if (!has_double_quotes)
      {
         quoting_char = '"';
      }

   }

   // if here, the size of the string is appropriate,
   // so do the copy
   memset(where, 0, max_length);

   if (needs_quotes)
   {
      memcpy(&where[index], &quoting_char, sizeof(quoting_char));
      index += sizeof(quoting_char);
   }

   memcpy(&where[index], value, strlen(value));
   index += strlen(value);

   if (needs_quotes)
   {
      memcpy(&where[index], &quoting_char, sizeof(quoting_char));
      index += sizeof(quoting_char);
   }

   where[index] = '\0';

   return 0;
}

/**
 * An utility function to convert the enumeration of values for the update_process_title
 * into one of its possible string descriptions.
 *
 * @param where the buffer used to store the stringy thing
 * @param value the config->update_process_title setting
 * @return 0 on success, 1 otherwise
 */
static int
to_update_process_title(char* where, int value)
{
   if (!where || value < 0)
   {
      return 1;
   }

   switch (value)
   {
      case UPDATE_PROCESS_TITLE_VERBOSE:
         snprintf(where, MISC_LENGTH, "%s", "verbose");
         break;
      case UPDATE_PROCESS_TITLE_MINIMAL:
         snprintf(where, MISC_LENGTH, "%s", "minimal");

         break;
      case UPDATE_PROCESS_TITLE_STRICT:
         snprintf(where, MISC_LENGTH, "%s", "strict");
         break;
      case UPDATE_PROCESS_TITLE_NEVER:
         snprintf(where, MISC_LENGTH, "%s", "never");
         break;
   }
   return 0;
}

/**
 * An utility function to convert the enumeration of values for the validation setting
 * into one of its possible string descriptions.
 *
 * @param where the buffer used to store the stringy thing
 * @param value the config->validation setting
 * @return 0 on success, 1 otherwise
 */
static int
to_validation(char* where, int value)
{

   if (!where || value < 0)
   {
      return 1;
   }

   switch (value)
   {
      case VALIDATION_OFF:
         snprintf(where, MISC_LENGTH, "%s", "off");
         break;
      case VALIDATION_FOREGROUND:
         snprintf(where, MISC_LENGTH, "%s", "foreground");
         break;
      case VALIDATION_BACKGROUND:
         snprintf(where, MISC_LENGTH, "%s", "background");
         break;
   }

   return 0;

}

/**
 * An utility function to convert the enumeration of values for the pipeline setting
 * into one of its possible string descriptions.
 *
 * @param where the buffer used to store the stringy thing
 * @param value the config->pipeline setting
 * @return 0 on success, 1 otherwise
 */
static int
to_pipeline(char* where, int value)
{
   if (!where || value < 0)
   {
      return 1;
   }

   switch (value)
   {
      case PIPELINE_AUTO:
         snprintf(where, MISC_LENGTH, "%s", "auto");
         break;
      case PIPELINE_SESSION:
         snprintf(where, MISC_LENGTH, "%s", "session");
         break;
      case PIPELINE_TRANSACTION:
         snprintf(where, MISC_LENGTH, "%s", "transaction");
         break;
      case PIPELINE_PERFORMANCE:
         snprintf(where, MISC_LENGTH, "%s", "performance");
         break;
   }

   return 0;
}

/**
 * An utility function to convert the enumeration of values for the log_level setting
 * into one of its possible string descriptions.
 *
 * @param where the buffer used to store the stringy thing
 * @param value the config->common.log_level setting
 * @return 0 on success, 1 otherwise
 */
static int
to_log_level(char* where, int value)
{
   if (!where || value < 0)
   {
      return 1;
   }

   switch (value)
   {

      case PGAGROAL_LOGGING_LEVEL_DEBUG2:
         snprintf(where, MISC_LENGTH, "%s", "debug2");
         break;
      case PGAGROAL_LOGGING_LEVEL_DEBUG1:
         snprintf(where, MISC_LENGTH, "%s", "debug");
         break;
      case PGAGROAL_LOGGING_LEVEL_INFO:
         snprintf(where, MISC_LENGTH, "%s", "info");
         break;
      case PGAGROAL_LOGGING_LEVEL_WARN:
         snprintf(where, MISC_LENGTH, "%s", "warn");
         break;
      case PGAGROAL_LOGGING_LEVEL_ERROR:
         snprintf(where, MISC_LENGTH, "%s", "error");
         break;
      case PGAGROAL_LOGGING_LEVEL_FATAL:
         snprintf(where, MISC_LENGTH, "%s", "fatal");
         break;

   }

   return 0;
}

/**
 * An utility function to convert the enumeration of values for the log_level setting
 * into one of its possible string descriptions.
 *
 * @param where the buffer used to store the stringy thing
 * @param value the config->common.log_mode setting
 * @return 0 on success, 1 otherwise
 */
static int
to_log_mode(char* where, int value)
{
   if (!where || value < 0)
   {
      return 1;
   }

   switch (value)
   {

      case PGAGROAL_LOGGING_MODE_CREATE:
         snprintf(where, MISC_LENGTH, "%s", "create");
         break;
      case PGAGROAL_LOGGING_MODE_APPEND:
         snprintf(where, MISC_LENGTH, "%s", "append");
         break;
   }

   return 0;
}

/**
 * An utility function to convert the enumeration of values for the log_type setting
 * into one of its possible string descriptions.
 *
 * @param where the buffer used to store the stringy thing
 * @param value the config->common.log_type setting
 * @return 0 on success, 1 otherwise
 */
static int
to_log_type(char* where, int value)
{
   if (!where || value < 0)
   {
      return 1;
   }

   switch (value)
   {
      case PGAGROAL_LOGGING_TYPE_CONSOLE:
         snprintf(where, MISC_LENGTH, "%s", "console");
         break;
      case PGAGROAL_LOGGING_TYPE_FILE:
         snprintf(where, MISC_LENGTH, "%s", "file");
         break;
      case PGAGROAL_LOGGING_TYPE_SYSLOG:
         snprintf(where, MISC_LENGTH, "%s", "syslog");
         break;

   }

   return 0;
}

int
pgagroal_apply_main_configuration(struct main_configuration* config,
                                  struct server* srv,
                                  char* section,
                                  char* key,
                                  char* value)
{
   size_t max = 0;
   bool unknown = false;

   //   pgagroal_log_trace( "Configuration setting [%s] <%s> -> <%s>", section, key, value );

   if (key_in_section("host", section, key, true, NULL))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->common.host, value, max);
   }
   else if (key_in_section("host", section, key, false, &unknown))
   {
      max = strlen(section);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->name, section, max);
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->host, value, max);
      atomic_store(&srv->state, SERVER_NOTINIT);
   }
   else if (key_in_section("port", section, key, true, NULL))
   {
      if (as_int(value, &config->common.port))
      {
         unknown = true;
      }
   }
   else if (key_in_section("port", section, key, false, &unknown))
   {
      memcpy(&srv->name, section, strlen(section));
      if (as_int(value, &srv->port))
      {
         unknown = true;
      }
      atomic_store(&srv->state, SERVER_NOTINIT);
   }
   else if (key_in_section("primary", section, key, false, &unknown))
   {
      bool b = false;
      if (as_bool(value, &b))
      {
         unknown = true;
      }
      if (b)
      {
         atomic_store(&srv->state, SERVER_NOTINIT_PRIMARY);
      }
      else
      {
         atomic_store(&srv->state, SERVER_NOTINIT);
      }
   }
   else if (key_in_section("metrics", section, key, true, &unknown))
   {
      if (as_int(value, &config->metrics))
      {
         unknown = true;
      }
   }
   else if (key_in_section("metrics_cache_max_age", section, key, true, &unknown))
   {
      if (as_seconds(value, &config->metrics_cache_max_age, PGAGROAL_PROMETHEUS_CACHE_DISABLED))
      {
         unknown = true;
      }
   }
   else if (key_in_section("metrics_cache_max_size", section, key, true, &unknown))
   {
      if (as_bytes(value, &config->metrics_cache_max_size, PROMETHEUS_DEFAULT_CACHE_SIZE))
      {
         unknown = true;
      }
   }
   else if (key_in_section("management", section, key, true, &unknown))
   {
      if (as_int(value, &config->management))
      {
         unknown = true;
      }
   }
   else if (key_in_section("pipeline", section, key, true, &unknown))
   {
      config->pipeline = as_pipeline(value);
   }
   else if (key_in_section("failover", section, key, true, &unknown))
   {
      if (as_bool(value, &config->failover))
      {
         unknown = true;
      }
   }
   else if (key_in_section("failover_script", section, key, true, &unknown))
   {

      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->failover_script, value, max);
   }
   else if (key_in_section("auth_query", section, key, true, &unknown))
   {
      if (as_bool(value, &config->authquery))
      {
         unknown = true;
      }
   }
   else if (key_in_section("tls", section, key, true, &unknown))
   {
      if (as_bool(value, &config->tls))
      {
         unknown = true;
      }
   }
   else if (key_in_section("tls", section, key, false, &unknown))
   {
      if (as_bool(value, &srv->tls))
      {
         unknown = true;
      }
   }
   else if (key_in_section("tls_ca_file", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->tls_ca_file, value, max);
   }
   else if (key_in_section("tls_ca_file", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(srv->tls_ca_file, value, max);
   }
   else if (key_in_section("tls_cert_file", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->tls_cert_file, value, max);
   }
   else if (key_in_section("tls_cert_file", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(srv->tls_cert_file, value, max);
   }
   else if (key_in_section("tls_key_file", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->tls_key_file, value, max);
   }
   else if (key_in_section("tls_key_file", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(srv->tls_key_file, value, max);
   }
   else if (key_in_section("blocking_timeout", section, key, true, &unknown))
   {

      if (as_int(value, &config->blocking_timeout))
      {
         unknown = true;
      }
   }
   else if (key_in_section("idle_timeout", section, key, true, &unknown))
   {
      if (as_int(value, &config->idle_timeout))
      {
         unknown = true;
      }
   }
   else if (key_in_section("rotate_frontend_password_timeout", section, key, true, &unknown))
   {
      if (as_int(value, &config->rotate_frontend_password_timeout))
      {
         unknown = true;
      }
   }
   else if (key_in_section("rotate_frontend_password_length", section, key, true, &unknown))
   {
      if (as_int(value, &config->rotate_frontend_password_length))
      {
         unknown = true;
      }
   }
   else if (key_in_section("max_connection_age", section, key, true, &unknown))
   {
      if (as_int(value, &config->max_connection_age))
      {
         unknown = true;
      }
   }
   else if (key_in_section("validation", section, key, true, &unknown))
   {
      config->validation = as_validation(value);
   }
   else if (key_in_section("background_interval", section, key, true, &unknown))
   {
      if (as_int(value, &config->background_interval))
      {
         unknown = true;
      }
   }
   else if (key_in_section("max_retries", section, key, true, &unknown))
   {
      if (as_int(value, &config->max_retries))
      {
         unknown = true;
      }
   }
   else if (key_in_section("authentication_timeout", section, key, true, &unknown))
   {
      if (as_int(value, &config->authentication_timeout))
      {
         unknown = true;
      }
   }
   else if (key_in_section("disconnect_client", section, key, true, &unknown))
   {
      if (as_int(value, &config->disconnect_client))
      {
         unknown = true;
      }
   }
   else if (key_in_section("disconnect_client_force", section, key, true, &unknown))
   {
      if (as_bool(value, &config->disconnect_client_force))
      {
         unknown = true;
      }
   }
   else if (key_in_section("pidfile", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->pidfile, value, max);
   }
   else if (key_in_section("allow_unknown_users", section, key, true, &unknown))
   {
      if (as_bool(value, &config->allow_unknown_users))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_type", section, key, true, &unknown))
   {
      config->common.log_type = as_logging_type(value);
   }
   else if (key_in_section("log_level", section, key, true, &unknown))
   {
      config->common.log_level = as_logging_level(value);
   }
   else if (key_in_section("log_path", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->common.log_path, value, max);
   }
   else if (key_in_section("log_rotation_size", section, key, true, &unknown))
   {
      if (as_logging_rotation_size(value, &config->common.log_rotation_size))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_rotation_age", section, key, true, &unknown))
   {
      if (as_logging_rotation_age(value, &config->common.log_rotation_age))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_line_prefix", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }

      memcpy(config->common.log_line_prefix, value, max);
   }
   else if (key_in_section("log_connections", section, key, true, &unknown))
   {

      if (as_bool(value, &config->common.log_connections))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_disconnections", section, key, true, &unknown))
   {
      if (as_bool(value, &config->common.log_disconnections))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_mode", section, key, true, &unknown))
   {
      config->common.log_mode = as_logging_mode(value);
   }
   else if (key_in_section("max_connections", section, key, true, &unknown))
   {
      if (as_int(value, &config->max_connections))
      {
         unknown = true;
      }
   }
   else if (key_in_section("unix_socket_dir", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->unix_socket_dir, value, max);
   }
   else if (key_in_section("master_key_file_location", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->master_key_file_location, value, max);
   }
   else if (key_in_section("libev", section, key, true, &unknown))
   {

      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->libev, value, max);
   }
   else if (key_in_section("buffer_size", section, key, true, &unknown))
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
   else if (key_in_section("keep_alive", section, key, true, &unknown))
   {
      if (as_bool(value, &config->keep_alive))
      {
         unknown = true;
      }
   }
   else if (key_in_section("nodelay", section, key, true, &unknown))
   {
      if (as_bool(value, &config->nodelay))
      {
         unknown = true;
      }
   }
   else if (key_in_section("non_blocking", section, key, true, &unknown))
   {
      if (as_bool(value, &config->non_blocking))
      {
         unknown = true;
      }
   }
   else if (key_in_section("backlog", section, key, true, &unknown))
   {
      if (as_int(value, &config->backlog))
      {
         unknown = true;
      }
   }
   else if (key_in_section("hugepage", section, key, true, &unknown))
   {
      config->hugepage = as_hugepage(value);
   }
   else if (key_in_section("tracker", section, key, true, &unknown))
   {
      if (as_bool(value, &config->tracker))
      {
         unknown = true;
      }
   }
   else if (key_in_section("track_prepared_statements", section, key, true, &unknown))
   {
      if (as_bool(value, &config->track_prepared_statements))
      {
         unknown = true;
      }
   }
   else if (key_in_section("update_process_title", section, key, true, &unknown))
   {
      if (as_update_process_title(value, &config->update_process_title, UPDATE_PROCESS_TITLE_VERBOSE))
      {
         unknown = false;
      }
   }
   else
   {
      unknown = true;
   }

   if (unknown)
   {
      return 1;
   }
   else
   {
      return 0;
   }
}

int
pgagroal_apply_vault_configuration(struct vault_configuration* config,
                                   struct vault_server* srv,
                                   char* section,
                                   char* key,
                                   char* value)
{
   size_t max = 0;
   bool unknown = false;

   //   pgagroal_log_trace( "Configuration setting [%s] <%s> -> <%s>", section, key, value );

   if (key_in_section("host", section, key, true, NULL))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->common.host, value, max);
   }
   else if (key_in_section("host", section, key, false, &unknown))
   {
      max = strlen(section);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->server.name, section, max);
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->server.host, value, max);
   }
   else if (key_in_section("port", section, key, true, NULL))
   {
      if (as_int(value, &config->common.port))
      {
         unknown = true;
      }
   }
   else if (key_in_section("port", section, key, false, &unknown))
   {
      memcpy(&srv->server.name, section, strlen(section));
      if (as_int(value, &srv->server.port))
      {
         unknown = true;
      }
   }
   else if (key_in_section("user", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->user.username, value, max);
   }
   else if (key_in_section("tls", section, key, false, &unknown))
   {
      if (as_bool(value, &srv->server.tls))
      {
         unknown = true;
      }
   }
   else if (key_in_section("tls_ca_file", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->server.tls_ca_file, value, max);
   }
   else if (key_in_section("tls_cert_file", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->server.tls_cert_file, value, max);
   }
   else if (key_in_section("tls_key_file", section, key, false, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(&srv->server.tls_key_file, value, max);
   }
   else if (key_in_section("log_type", section, key, true, &unknown))
   {
      config->common.log_type = as_logging_type(value);
   }
   else if (key_in_section("log_level", section, key, true, &unknown))
   {
      config->common.log_level = as_logging_level(value);
   }
   else if (key_in_section("log_path", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }
      memcpy(config->common.log_path, value, max);
   }
   else if (key_in_section("log_rotation_size", section, key, true, &unknown))
   {
      if (as_logging_rotation_size(value, &config->common.log_rotation_size))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_rotation_age", section, key, true, &unknown))
   {
      if (as_logging_rotation_age(value, &config->common.log_rotation_age))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_line_prefix", section, key, true, &unknown))
   {
      max = strlen(value);
      if (max > MISC_LENGTH - 1)
      {
         max = MISC_LENGTH - 1;
      }

      memcpy(config->common.log_line_prefix, value, max);
   }
   else if (key_in_section("log_connections", section, key, true, &unknown))
   {

      if (as_bool(value, &config->common.log_connections))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_disconnections", section, key, true, &unknown))
   {
      if (as_bool(value, &config->common.log_disconnections))
      {
         unknown = true;
      }
   }
   else if (key_in_section("log_mode", section, key, true, &unknown))
   {
      config->common.log_mode = as_logging_mode(value);
   }
   return 0;
}

int
pgagroal_apply_configuration(char* config_key, char* config_value)
{
   struct main_configuration* config;
   struct main_configuration* current_config;

   char section[MISC_LENGTH];
   char context[MISC_LENGTH];
   char key[MISC_LENGTH];
   int begin = -1, end = -1;
   bool main_section;
   size_t config_size = 0;
   struct server* srv_dst;
   struct server* srv_src;

   // get the currently running configuration
   current_config = (struct main_configuration*)shmem;
   // create a new configuration that will be the clone of the previous one
   config_size = sizeof(struct main_configuration);
   if (pgagroal_create_shared_memory(config_size, HUGEPAGE_OFF, (void**)&config))
   {
      goto error;
   }

   // copy the configuration that is currently running
   memcpy(config, current_config, config_size);

   memset(section, 0, MISC_LENGTH);
   memset(context, 0, MISC_LENGTH);
   memset(key, 0, MISC_LENGTH);

   for (int i = 0; i < strlen(config_key); i++)
   {
      if (config_key[i] == '.')
      {
         if (!strlen(section))
         {
            memcpy(section, &config_key[begin], end - begin + 1);
            section[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }
         else if (!strlen(context))
         {
            memcpy(context, &config_key[begin], end - begin + 1);
            context[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }
         else if (!strlen(key))
         {
            memcpy(key, &config_key[begin], end - begin + 1);
            key[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }

      }

      if (begin < 0)
      {
         begin = i;
      }

      end = i;

   }

   // if the key has not been found, since there is no ending dot,
   // try to extract it from the string
   if (!strlen(key))
   {
      memcpy(key, &config_key[begin], end - begin + 1);
      key[end - begin + 1] = '\0';
   }

   // force the main section, i.e., global parameters, if and only if
   // there is no section or section is 'pgagroal' without any subsection
   main_section = (!strlen(section) || !strncmp(section, PGAGROAL_MAIN_INI_SECTION, MISC_LENGTH))
                  && !strlen(context);

   if (!strncmp(section, PGAGROAL_CONF_SERVER_PREFIX, MISC_LENGTH))
   {
      srv_src = srv_dst = NULL;

      // server.<servername>.<key>
      // here the 'context' is the server name, so let's find it
      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (!strncmp(config->servers[i].name, context, MISC_LENGTH))
         {
            pgagroal_log_debug("Changing configuration of server <%s>: (%s) %s -> %s",
                               config->servers[i].name,
                               config_key,
                               key,
                               config_value);

            srv_dst = calloc(1, sizeof(struct server));
            srv_src = &config->servers[i];
            // clone the current server
            memcpy(srv_dst, srv_src, sizeof(struct server));
            if (pgagroal_apply_main_configuration(config,
                                                  srv_dst,
                                                  context,
                                                  key,
                                                  config_value))
            {
               goto error;
            }

            // now that changes have been applied, see if the server
            // requires a restart: in such case abort the configuration
            // change
            if (restart_server(srv_dst, srv_src))
            {
               goto error;
            }

            break;    // avoid searching for another server section

         }
      }

      memcpy(srv_src, srv_dst, sizeof(struct server));
      srv_src = srv_dst = NULL;

   }
   else if (!strncmp(section, PGAGROAL_CONF_HBA_PREFIX, MISC_LENGTH))
   {
      // hba.<user>.<key>
      // here the context is the username
      // and the section is the 'hba', while the key is what the user wants to change
      for (int i = 0; i < config->number_of_hbas; i++)
      {
         if (!strncmp(config->hbas[i].username, context, MISC_LENGTH))
         {
            // this is the correct HBA entry, apply the changes
            pgagroal_log_debug("Trying to change HBA configuration setting <%s> to <%s>", key, config_value);
            if (pgagroal_apply_hba_configuration(&config->hbas[i], key, config_value))
            {
               goto error;
            }

            break;    // avoid searching for another HBA entry
         }
      }
   }
   else if (!strncmp(section, PGAGROAL_CONF_LIMIT_PREFIX, MISC_LENGTH))
   {
      // limit.<user>.<key>
      // the context is the username and the key is what to change
      for (int i = 0; i < config->number_of_limits; i++)
      {
         if (!strncmp(config->limits[i].username, context, MISC_LENGTH))
         {
            // this is the correct limit entry, apply the changes
            // WARNING: according to restart_limit() every change to a limit entry
            // requires a restart, so it does not make a lot of sense to apply a configuration change
            pgagroal_log_debug("Trying to change limit configuration setting <%s> to <%s>", key, config_value);
            if (pgagroal_apply_limit_configuration_string(&config->limits[i], key, config_value))
            {
               goto error;
            }

            break;    // avoid searching for another HBA entry
         }
      }
      //      return pgagroal_write_limit_config_value(buffer, context, key);
   }
   else if (main_section)
   {

      pgagroal_log_debug("Trying to change main configuration setting <%s> to <%s>", config_key, config_value);
      if (pgagroal_apply_main_configuration(config,
                                            NULL,
                                            PGAGROAL_MAIN_INI_SECTION,
                                            config_key,
                                            config_value))
      {
         goto error;
      }
   }
   else
   {
      // if here, an error happened!
      goto error;
   }

   if (pgagroal_validate_configuration(config, false, false))
   {
      goto error;
   }

   if (transfer_configuration(current_config, config))
   {
      goto error;
   }

   if (pgagroal_destroy_shared_memory((void*)config, config_size))
   {
      goto error;
   }

   // all done
   return 0;
error:

   if (config != NULL)
   {
      memcpy(config, current_config, sizeof(struct main_configuration));
      pgagroal_destroy_shared_memory((void*)config, config_size);
   }

   return 1;
}

/**
 * Utility function to set an HBA single entry.
 * The HBA entry must be already allocated.
 *
 * Before applying a setting, the field is zeroed.
 *
 * @param hba the entry to modify
 * @param context the entry to modify, e.g., "method" or a constant like PGAGRAOL_HBA_ENTRY_DATABASE
 * @param value the value to set
 *
 * @return 0 on success, 1 on failure
 */
static int
pgagroal_apply_hba_configuration(struct hba* hba,
                                 char* context,
                                 char* value)
{

   if (!hba || !context || !strlen(context) || !value || !strlen(value))
   {
      goto error;
   }

   if (!strncmp(context, PGAGROAL_HBA_ENTRY_TYPE, MAX_TYPE_LENGTH)
       && strlen(value) < MAX_TYPE_LENGTH)
   {
      memset(&(hba->type), 0, strlen(hba->type));
      memcpy(&(hba->type), value, strlen(value));
   }
   else if (!strncmp(context, PGAGROAL_HBA_ENTRY_DATABASE, MAX_DATABASE_LENGTH)
            && strlen(value) < MAX_DATABASE_LENGTH)
   {
      memset(&(hba->database), 0, strlen(hba->database));
      memcpy(&(hba->database), value, strlen(value));
   }
   else if (!strncmp(context, PGAGROAL_HBA_ENTRY_USERNAME, MAX_USERNAME_LENGTH)
            && strlen(value) < MAX_USERNAME_LENGTH)
   {
      memset(&(hba->username), 0, strlen(hba->username));
      memcpy(&(hba->username), value, strlen(value));
   }
   else if (!strncmp(context, PGAGROAL_HBA_ENTRY_ADDRESS, MAX_ADDRESS_LENGTH)
            && strlen(value) < MAX_ADDRESS_LENGTH)
   {
      memset(&(hba->address), 0, strlen(hba->address));
      memcpy(&(hba->address), value, strlen(value));
   }
   else if (!strncmp(context, PGAGROAL_HBA_ENTRY_METHOD, MAX_ADDRESS_LENGTH)
            && strlen(value) < MAX_ADDRESS_LENGTH)
   {
      memset(&(hba->method), 0, strlen(hba->method));
      memcpy(&(hba->method), value, strlen(value));
   }

   return 0;

error:
   return 1;
}

/**
 * An utility function to set a single value for the limit struct.
 * The structure must already be allocated.
 *
 * @param limit the structure to change
 * @param context the key of the field to change, e.g., 'max_size' or a constant like PGAGROAL_LIMIT_ENTRY_DATABASE
 * @param value the new value to set
 *
 * @return 0 on success.
 */
static int
pgagroal_apply_limit_configuration_string(struct limit* limit,
                                          char* context,
                                          char* value)
{

   if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_DATABASE, MAX_DATABASE_LENGTH)
       && strlen(value) < MAX_DATABASE_LENGTH)
   {
      memset(&limit->database, 0, strlen(limit->database));
      memcpy(&limit->database, value, strlen(value));
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_USERNAME, MAX_USERNAME_LENGTH)
            && strlen(value) < MAX_USERNAME_LENGTH)
   {
      memset(&limit->username, 0, strlen(limit->username));
      memcpy(&limit->username, value, strlen(value));
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_MAX_SIZE, MISC_LENGTH))
   {
      return as_int(value, &limit->max_size);
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_MIN_SIZE, MISC_LENGTH))
   {
      return as_int(value, &limit->min_size);
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_INITIAL_SIZE, MISC_LENGTH))
   {
      return as_int(value, &limit->initial_size);
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_LINENO, MISC_LENGTH))
   {
      return as_int(value, &limit->lineno);
   }
   else
   {
      goto error;
   }

   return 0;

error:
   return 1;

}

static int
pgagroal_apply_limit_configuration_int(struct limit* limit,
                                       char* context,
                                       int value)
{

   if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_MAX_SIZE, MISC_LENGTH))
   {
      limit->max_size = value;
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_MIN_SIZE, MISC_LENGTH))
   {
      limit->min_size = value;
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_INITIAL_SIZE, MISC_LENGTH))
   {
      limit->initial_size = value;
   }
   else if (!strncmp(context, PGAGROAL_LIMIT_ENTRY_LINENO, MISC_LENGTH))
   {
      limit->lineno = value;
   }
   else
   {
      goto error;
   }

   return 0;

error:
   return 1;

}
