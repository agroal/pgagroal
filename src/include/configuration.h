/*
 * Copyright (C) 2025 The pgagroal community
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

#ifndef PGAGROAL_CONFIGURATION_H
#define PGAGROAL_CONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <json.h>

/*
 * The main section that must be present in the `pgagroal.conf`
 * configuration file.
 */
#define PGAGROAL_MAIN_INI_SECTION "pgagroal"
/*
 * The main section that must be present in the `pgagroal_vault.conf`
 * configuration file.
 */
#define PGAGROAL_VAULT_INI_SECTION "pgagroal-vault"

/*
 * The following constants are used to clearly identify
 * a section the user wants to get configuration
 * or change. They are used in the config-get
 * and config-set operations.
 */
#define PGAGROAL_CONF_SERVER_PREFIX "server"
#define PGAGROAL_CONF_HBA_PREFIX    "hba"
#define PGAGROAL_CONF_LIMIT_PREFIX  "limit"

/**
 * Status that pgagroal_read_configuration() could provide.
 * Use only negative values for errors, since a positive return
 * value will indicate the number of problems within sections.
 */
#define PGAGROAL_CONFIGURATION_STATUS_OK 0
#define PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND -1
#define PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG -2
#define PGAGROAL_CONFIGURATION_STATUS_KO -3
#define PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT -4

#define CONFIGURATION_ARGUMENT_MAIN_CONF_PATH           "main_configuration_path"
#define CONFIGURATION_ARGUMENT_LIMIT_CONF_PATH          "limit_configuration_path"
#define CONFIGURATION_ARGUMENT_HBA_CONF_PATH            "hba_configuration_path"
#define CONFIGURATION_ARGUMENT_USER_CONF_PATH           "users_configuration_path"
#define CONFIGURATION_ARGUMENT_FRONTEND_USERS_CONF_PATH "frontend_users_configuration_path"
#define CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH          "admin_configuration_path"
#define CONFIGURATION_ARGUMENT_SUPERUSER_CONF_PATH      "superuser_configuration_path"

#define CONFIGURATION_ARGUMENT_HOST                             "host"
#define CONFIGURATION_ARGUMENT_PORT                             "port"
#define CONFIGURATION_ARGUMENT_UNIX_SOCKET_DIR                  "unix_socket_dir"
#define CONFIGURATION_ARGUMENT_METRICS                          "metrics"
#define CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE            "metrics_cache_max_age"
#define CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE           "metrics_cache_max_size"
#define CONFIGURATION_ARGUMENT_MANAGEMENT                       "management"
#define CONFIGURATION_ARGUMENT_LOG_TYPE                         "log_type"
#define CONFIGURATION_ARGUMENT_LOG_LEVEL                        "log_level"
#define CONFIGURATION_ARGUMENT_LOG_PATH                         "log_path"
#define CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE                 "log_rotation_age"
#define CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE                "log_rotation_size"
#define CONFIGURATION_ARGUMENT_LOG_LINE_PREFIX                  "log_line_prefix"
#define CONFIGURATION_ARGUMENT_LOG_MODE                         "log_mode"
#define CONFIGURATION_ARGUMENT_LOG_CONNECTIONS                  "log_connections"
#define CONFIGURATION_ARGUMENT_LOG_DISCONNECTIONS               "log_disconnections"
#define CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT                 "blocking_timeout"
#define CONFIGURATION_ARGUMENT_IDLE_TIMEOUT                     "idle_timeout"
#define CONFIGURATION_ARGUMENT_ROTATE_FRONTEND_PASSWORD_TIMEOUT "rotate_frontend_password_timeout"
#define CONFIGURATION_ARGUMENT_ROTATE_FRONTEND_PASSWORD_LENGTH  "rotate_frontend_password_length"
#define CONFIGURATION_ARGUMENT_MAX_CONNECTION_AGE               "max_connection_age"
#define CONFIGURATION_ARGUMENT_VALIDATION                       "validation"
#define CONFIGURATION_ARGUMENT_BACKGROUND_INTERVAL              "background_interval"
#define CONFIGURATION_ARGUMENT_MAX_RETRIES                      "max_retries"
#define CONFIGURATION_ARGUMENT_MAX_CONNECTIONS                  "max_connections"
#define CONFIGURATION_ARGUMENT_ALLOW_UNKNOWN_USERS              "allow_unknown_users"
#define CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT           "authentication_timeout"
#define CONFIGURATION_ARGUMENT_PIPELINE                         "pipeline"
#define CONFIGURATION_ARGUMENT_AUTH_QUERY                       "auth_query"
#define CONFIGURATION_ARGUMENT_FAILOVER                         "failover"
#define CONFIGURATION_ARGUMENT_FAILOVER_SCRIPT                  "failover_script"
#define CONFIGURATION_ARGUMENT_TLS                              "tls"
#define CONFIGURATION_ARGUMENT_TLS_CERT_FILE                    "tls_cert_file"
#define CONFIGURATION_ARGUMENT_TLS_KEY_FILE                     "tls_key_file"
#define CONFIGURATION_ARGUMENT_TLS_CA_FILE                      "tls_ca_file"
#define CONFIGURATION_ARGUMENT_METRICS_CERT_FILE                "metrics_cert_file"
#define CONFIGURATION_ARGUMENT_METRICS_KEY_FILE                 "metrics_key_file"
#define CONFIGURATION_ARGUMENT_METRICS_CA_FILE                  "metrics_ca_file"
#define CONFIGURATION_ARGUMENT_EV_BACKEND                       "ev_backend"
#define CONFIGURATION_ARGUMENT_KEEP_ALIVE                       "keep_alive"
#define CONFIGURATION_ARGUMENT_NODELAY                          "nodelay"
#define CONFIGURATION_ARGUMENT_NON_BLOCKING                     "non_blocking"
#define CONFIGURATION_ARGUMENT_BACKLOG                          "backlog"
#define CONFIGURATION_ARGUMENT_HUGEPAGE                         "hugepage"
#define CONFIGURATION_ARGUMENT_TRACKER                          "tracker"
#define CONFIGURATION_ARGUMENT_TRACK_PREPARED_STATEMENTS        "track_prepared_statements"
#define CONFIGURATION_ARGUMENT_PIDFILE                          "pidfile"
#define CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE             "update_process_title"
#define CONFIGURATION_ARGUMENT_PRIMARY                          "primary"

/**
 * Initialize the configuration structure
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_init_configuration(void* shmem);

/**
 * Initialize the vault configuration structure
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_vault_init_configuration(void* shmem);

/**
 * Read the configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @param emit_warnings true if unknown parameters have to
 *        reported on stderr
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many sections
 *         - a positive value to indicate how many errors (with regard to sections) have been found
 *         - PGAGROAL_CONFIGURATION_STATUS_KO if the file has generic errors, most notably it lacks
 *           a [pgagroal] section
 */
int
pgagroal_read_configuration(void* shmem, char* filename, bool emit_warnings);

/**
 * Validate the configuration
 * @param shmem The shared memory segment
 * @param has_unix_socket Has Unix socket
 * @param has_main_sockets Has main sockets
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_configuration(void* shmem, bool has_unix_socket, bool has_main_sockets);

/**
 * Read the configuration of vault from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @param emit_warnings true if unknown parameters have to
 *        reported on stderr
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many sections
 *         - a positive value to indicate how many errors (with regard to sections) have been found
 *         - PGAGROAL_CONFIGURATION_STATUS_KO if the file has generic errors, most notably it lacks
 *           a [pgagroal-vault] section
 */
int
pgagroal_vault_read_configuration(void* shmem, char* filename, bool emit_warnings);

/**
 * Validate the configuration of vault
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_vault_validate_configuration(void* shmem);

/**
 * Read the HBA configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many entries
 */
int
pgagroal_read_hba_configuration(void* shmem, char* filename);

/**
 * Validate the HBA configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_hba_configuration(void* shmem);

/**
 * Read the LIMIT configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many limits
 */
int
pgagroal_read_limit_configuration(void* shmem, char* filename);

/**
 * Validate the LIMIT configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_limit_configuration(void* shmem);

/**
 * Read the USERS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many users
 *           (i.e., more users than the number defined in the limits)
 *         - PGAGROAL_CONFIGURATION_STATUS_KO if the file has some problem (e.g., cannot be decrypted)
 *         - PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT to indicate a problem reading the content of the file

 */
int
pgagroal_read_users_configuration(void* shmem, char* filename);

/**
 * Validate the USERS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_users_configuration(void* shmem);

/**
 * Read the FRONTEND USERS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many users
 *           (i.e., more users than the number defined in the limits)
 *         - PGAGROAL_CONFIGURATION_STATUS_KO if the file has some problem (e.g., cannot be decrypted)
 *         - PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT to indicate a problem reading the content of the file
 */
int
pgagroal_read_frontend_users_configuration(void* shmem, char* filename);

/**
 * Validate the FRONTEND USERS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_frontend_users_configuration(void* shmem);

/**
 * Read the ADMINS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many users
 *           (i.e., more users than the number defined in the limits)
 *         - PGAGROAL_CONFIGURATION_STATUS_KO if the file has some problem (e.g., cannot be decrypted)
 *         - PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT to indicate a problem reading the content of the file
 */
int
pgagroal_read_admins_configuration(void* shmem, char* filename);

/**
 * Read the USERS configuration of vault from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file contains too many users
 *           (i.e., more users than the number defined in the limits)
 *         - PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT to indicate a problem reading the content of the file
 */
int
pgagroal_vault_read_users_configuration(void* shmem, char* filename);

/**
 * Validate the ADMINS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_admins_configuration(void* shmem);

/**
 * Read the superuser from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 (i.e, PGAGROAL_CONFIGURATION_STATUS_OK) upon success, otherwise
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND if the file does not exists
 *         - PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG  if the file entry is to big
 *         - PGAGROAL_CONFIGURATION_STATUS_KO if the file has some problem (e.g., cannot be decrypted)
 *         - PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT to indicate a problem reading the content of the file
 */
int
pgagroal_read_superuser_configuration(void* shmem, char* filename);

/**
 * Validate the SUPERUSER configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_validate_superuser_configuration(void* shmem);

/**
 * Reload the configuration
 * @param reload Should the server be reloaded
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_reload_configuration(bool* reload);

/**
 * Automatically initialize the 'pidfile'
 * if none has been specified.
 * This function is called as last step
 * from pgagroal_validate_configuration
 * because it builds the pidfile on the value
 * of unix_socket_dir.
 */
void
pgagroal_init_pidfile_if_needed(void);

/**
 * Checks if the configuration has a min set of values to
 * take into account doing a prefill. For example, there must
 * be users and limits set, otherwise it does not
 * make any sense to attempt a prefill.
 * This can be used to wrap the condituion before calling
 * other prefill functions, e.g., `pgagroal_prefill()`.
 */
bool
pgagroal_can_prefill(void);

/**
 * Gets a configuration parameter and places into the string pointer.
 * This is used, for example, to get a writable string to send over the
 * management socket.
 *
 * The key can contain words separated by a dot '.' to indicate different search criterias.
 * A "dotted" key is made by a 'context', a 'section' and a 'search term', so that
 * it can be written as 'section.context.search'.
 * If both the section and the context are omitted, the 'search' is performed among the
 * pgagroal global settings (i.e., those under the [pgagroal] main section). The same
 * happens if the the section is specified as 'pgagroal', therefore the following two
 * terms do the same search:
 * - `update_process_title`
 * - `pgagroal.update_process_title`
 *
 * Other possible sections are:
 * - server to search for a specific server, the match is performed on the server name;
 * - hba to search for a specific HBA entry, the match is performed on the username;
 * - limit to search for a specific database in the limit (database) configuration file.
 *
 * When one the above sections is specified, the search is done identifying the entry to snoop
 * by means of 'context', and within such the 'search' is performed.
 *
 * In the case of the `server` section, the `context` has to be the name of a server configured,
 * while the `search` has to be the keyword to look for. AS an example: `server.venkman.port` provides
 * the value of the 'port' setting under the server section '[venkman]'.
 *
 * In the case of the 'hba` section, the `context` has to be a username as it appears in a line
 * of the pgagroal_hba.conf file, while the `search` has to be the column keyword to snoop.
 * For example, `hba.luca.method` will seek for the `method` used to authenticate the user `luca`.
 * Please note that, since the same user could be listed more than once, only the first matching
 * entry is reported.
 *
 * In the case of the 'limit` section, the `context` has to be a database name as it appears in a line
 * of the pgagroal_database.conf file, while the `search` has to be the column keyword to snoop.
 * For example, `limit.pgbench.max_size` will seek for the `max_size` connection limit for the
 * database 'pgbench'.
 * Please note that, since the same database could be listed more than once, only the first matching
 * entry is reported.
 *
 * @param buffer where to write the configuration value. The buffer must
 * be already allocated. In case of failure, the buffer is zero filled.
 * @param config_key the name of the configuration parameter
 * @param buffer_size the max length of the buffer where the result will be stored
 * @return 0 on success, 1 when the key cannot be found
 */
int
pgagroal_write_config_value(char* buffer, char* config_key, size_t buffer_size);

/**
 * Function to apply a single configuration parameter.
 *
 * This is the backbone function used when parsing the main configuration file
 * and is used to set any of the allowed parameters.
 *
 * @param config the configuration to apply values onto
 * @param srv the server to which the configuration parameter refers to, if needed
 * @param section the section of the file, main or server
 * @param key the parameter name of the configuration
 * @param value the value of the configuration
 * @return 0 on success
 *
 * Examples of usage:
 *   pgagroal_apply_main_configuration( config, NULL, PGAGROAL_MAIN_INI_SECTION, "log_level", "info" );
 */
int
pgagroal_apply_main_configuration(struct main_configuration* config,
                                  struct server* srv,
                                  char* section,
                                  char* key,
                                  char* value);

/**
 * Function to apply a single configuration parameter.
 *
 * This is the backbone function used when parsing the main configuration file
 * and is used to set any of the allowed parameters.
 *
 * @param config the configuration to apply values onto
 * @param srv the server to which the configuration parameter refers to, if needed
 * @param section the section of the file, main or server
 * @param key the parameter name of the configuration
 * @param value the value of the configuration
 * @return 0 on success
 *
 * Examples of usage:
 *   pgagroal_apply_vault_configuration( config, NULL, PGAGROAL_VAULT_INI_SECTION, "log_level", "info" );
 */
int
pgagroal_apply_vault_configuration(struct vault_configuration* config,
                                   struct vault_server* srv,
                                   char* section,
                                   char* key,
                                   char* value);

/**
 * Function to set a configuration value.
 *
 * This function accepts the same prefixes as the configuration get behavior, so
 * a single parameter like 'max_connections' is managed as the main configuration file,
 * a 'server' prefix will hit a specific server, a 'limit' prefix will set a limit, and so on.
 *
 * The idea behind the function is to "clone" the current configuration in use, and then
 * apply the changes. In order to be coherent to what a "reload" operation would do,
 * this function calls 'pgagroal_transfer_configuration' internally.
 *
 * @param config_key the string that contains the name of the parameter
 * @param config_value the value to set
 * @return 0 on success
 */
int
pgagroal_apply_configuration(char* config_key, char* config_value);

/**
 * Get a configuration parameter value
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgagroal_conf_get(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Set a configuration parameter value
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgagroal_conf_set(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

#ifdef __cplusplus
}
#endif

#endif
