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

static int transfer_configuration(struct configuration* config, struct configuration* reload);
static void copy_server(struct server* dst, struct server* src);
static void copy_hba(struct hba* dst, struct hba* src);
static void copy_user(struct user* dst, struct user* src);
static int restart_int(char* name, int e, int n);
static int restart_string(char* name, char* e, char* n);
static int restart_limit(char* name, struct configuration* config, struct configuration* reload);
static int restart_server(struct server* src, struct server* dst);

static bool is_empty_string(char* s);
static bool is_same_server(struct server* s1, struct server* s2);

static bool key_in_section(char* wanted, char* section, char* key, bool global, bool* unknown);
static bool is_comment_line(char* line);
static bool section_line(char* line, char* section);

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
pgagroal_read_configuration(void* shm, char* filename, bool emitWarnings)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* key = NULL;
   char* value = NULL;
   size_t max;
   struct configuration* config;
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
   config = (struct configuration*)shm;

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

               if (key_in_section("host", section, key, true, NULL))
               {
                  max = strlen(value);
                  if (max > MISC_LENGTH - 1)
                  {
                     max = MISC_LENGTH - 1;
                  }
                  memcpy(config->host, value, max);
               }
               else if (key_in_section("host", section, key, false, &unknown))
               {
                  max = strlen(section);
                  if (max > MISC_LENGTH - 1)
                  {
                     max = MISC_LENGTH - 1;
                  }
                  memcpy(&srv.name, section, max);
                  max = strlen(value);
                  if (max > MISC_LENGTH - 1)
                  {
                     max = MISC_LENGTH - 1;
                  }
                  memcpy(&srv.host, value, max);
                  atomic_store(&srv.state, SERVER_NOTINIT);
               }
               else if (key_in_section("port", section, key, true, NULL))
               {
                  if (as_int(value, &config->port))
                  {
                     unknown = true;
                  }
               }
               else if (key_in_section("port", section, key, false, &unknown))
               {
                  memcpy(&srv.name, section, strlen(section));
                  if (as_int(value, &srv.port))
                  {
                     unknown = true;
                  }
                  atomic_store(&srv.state, SERVER_NOTINIT);
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
                     atomic_store(&srv.state, SERVER_NOTINIT_PRIMARY);
                  }
                  else
                  {
                     atomic_store(&srv.state, SERVER_NOTINIT);
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
                  if (as_bool(value, &srv.tls))
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
               else if (key_in_section("tls_cert_file", section, key, true, &unknown))
               {
                  max = strlen(value);
                  if (max > MISC_LENGTH - 1)
                  {
                     max = MISC_LENGTH - 1;
                  }
                  memcpy(config->tls_cert_file, value, max);
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
                  config->log_type = as_logging_type(value);
               }
               else if (key_in_section("log_level", section, key, true, &unknown))
               {
                  config->log_level = as_logging_level(value);
               }
               else if (key_in_section("log_path", section, key, true, &unknown))
               {
                  max = strlen(value);
                  if (max > MISC_LENGTH - 1)
                  {
                     max = MISC_LENGTH - 1;
                  }
                  memcpy(config->log_path, value, max);
               }
               else if (key_in_section("log_rotation_size", section, key, true, &unknown))
               {
                  if (as_logging_rotation_size(value, &config->log_rotation_size))
                  {
                     unknown = true;
                  }
               }
               else if (key_in_section("log_rotation_age", section, key, true, &unknown))
               {
                  if (as_logging_rotation_age(value, &config->log_rotation_age))
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

                  memcpy(config->log_line_prefix, value, max);
               }
               else if (key_in_section("log_connections", section, key, true, &unknown))
               {

                  if (as_bool(value, &config->log_connections))
                  {
                     unknown = true;
                  }
               }
               else if (key_in_section("log_disconnections", section, key, true, &unknown))
               {
                  if (as_bool(value, &config->log_disconnections))
                  {
                     unknown = true;
                  }
               }
               else if (key_in_section("log_mode", section, key, true, &unknown))
               {
                  config->log_mode = as_logging_mode(value);
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

               if (unknown && emitWarnings)
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

   // check for duplicated servers
   for (int i = 0; i < config->number_of_servers; i++)
   {
      for (int j = i + 1; j < config->number_of_servers; j++)
      {
         if (is_same_server(&config->servers[i], &config->servers[j]))
         {
            pgagroal_log_fatal("pgagroal: Servers [%s] and [%s] are duplicated!",
                               config->servers[i].name,
                               config->servers[j].name);
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

   // do some last initialization here, since the configuration
   // looks good so far
   pgagroal_init_pidfile_if_needed();

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
      return PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
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
                  return PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
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
      return PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
   }

   index = 0;
   lineno = 0;
   config = (struct configuration*)shm;

   server_max = config->max_connections;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
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
                  return PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG;
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
   int status;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

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
   struct configuration* config;
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

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
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line) && !is_comment_line(line))
      {
         ptr = strtok(line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

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
   int status = PGAGROAL_CONFIGURATION_STATUS_OK;

   file = fopen(filename, "r");

   if (!file)
   {
      status = PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND;
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      status = PGAGROAL_CONFIGURATION_STATUS_KO;
      goto error;
   }

   index = 0;
   config = (struct configuration*)shm;

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

   if (pgagroal_read_configuration((void*)reload, config->configuration_path, true))
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
      k = malloc(c + 1);
      memset(k, 0, c + 1);
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
         v = malloc((c - offset) + 1);
         memset(v, 0, (c - offset) + 1);
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
         debug_value = (char*)malloc(size + 1);
         memset(debug_value, 0, size + 1);
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
            *initial_size = -1;
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

      if (offset <= length)
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
   config->metrics_cache_max_age = reload->metrics_cache_max_age;
   restart_int("metrics_cache_max_size", config->metrics_cache_max_size, reload->metrics_cache_max_size);
   config->management = reload->management;

   config->update_process_title = reload->update_process_title;

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
   // if the log main parameters have changed, we need
   // to restart the logging system
   if (strncmp(config->log_path, reload->log_path, MISC_LENGTH)
       || config->log_rotation_size != reload->log_rotation_size
       || config->log_rotation_age != reload->log_rotation_age
       || config->log_mode != reload->log_mode)
   {
      pgagroal_log_debug("Log restart triggered!");
      pgagroal_stop_logging();
      config->log_rotation_size = reload->log_rotation_size;
      config->log_rotation_age = reload->log_rotation_age;
      config->log_mode = reload->log_mode;
      memcpy(config->log_line_prefix, reload->log_line_prefix, MISC_LENGTH);
      memcpy(config->log_path, reload->log_path, MISC_LENGTH);
      pgagroal_start_logging();
   }

   config->log_connections = reload->log_connections;
   config->log_disconnections = reload->log_disconnections;

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

static int
restart_server(struct server* src, struct server* dst)
{
   char restart_message[MISC_LENGTH];

   if (!is_same_server(src, dst))
   {
      snprintf(restart_message, MISC_LENGTH, "Server <%s>, parameter <host>", src->name);
      restart_string(restart_message, dst->host, src->host);
      snprintf(restart_message, MISC_LENGTH, "Server <%s>, parameter <port>", src->name);
      restart_int(restart_message, dst->port, src->port);
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
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) == 0)
   {
      // no pidfile set, use a default one
      snprintf(config->pidfile, sizeof(config->pidfile), "%s/pgagroal.%d.pid",
               config->unix_socket_dir,
               config->port);
      pgagroal_log_debug("PID file automatically set to: [%s]", config->pidfile);
   }
}

bool
pgagroal_can_prefill(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

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
   if (global && !strncmp(section, PGAGROAL_MAIN_INI_SECTION, MISC_LENGTH))
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
