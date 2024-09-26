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
#include <logging.h>
#include <network.h>
#include <management.h>
#include <message.h>
#include <pool.h>
#include <utils.h>
#include <configuration.h>
#include <json.h>

/* system */
#include <cjson/cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MANAGEMENT_HEADER_SIZE 5
#define MANAGEMENT_INFO_SIZE 13

/**
 * JSON related command tags, used to build and retrieve
 * a JSON piece of information related to a single command
 */
#define JSON_TAG_COMMAND "command"
#define JSON_TAG_COMMAND_NAME "name"
#define JSON_TAG_COMMAND_STATUS "status"
#define JSON_TAG_COMMAND_ERROR "error"
#define JSON_TAG_COMMAND_OUTPUT "output"
#define JSON_TAG_COMMAND_EXIT_STATUS "exit-status"

#define JSON_TAG_APPLICATION_NAME "name"
#define JSON_TAG_APPLICATION_VERSION_MAJOR "major"
#define JSON_TAG_APPLICATION_VERSION_MINOR "minor"
#define JSON_TAG_APPLICATION_VERSION_PATCH "patch"
#define JSON_TAG_APPLICATION_VERSION "version"

#define JSON_TAG_ARRAY_NAME "list"

/**
 * JSON pre-defined values
 */
#define JSON_STRING_SUCCESS "OK"
#define JSON_STRING_ERROR   "KO"
#define JSON_BOOL_SUCCESS   0
#define JSON_BOOL_ERROR     1

#define S "S:"
#define V ",V:"

static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);
static int write_header(SSL* ssl, int fd, signed char type, int slot);
static int write_info(char* buffer, int command, int offset);

static int pgagroal_management_write_conf_ls_detail(int socket, char* what);
static int pgagroal_management_read_conf_ls_detail(SSL* ssl, int socket, char* buffer);

static int pgagroal_management_json_print_status_details(cJSON* json);

static cJSON* pgagroal_management_json_read_status_details(SSL* ssl, int socket, bool include_details);
static cJSON* pgagroal_managment_json_read_config_get(int socket, char* config_key, char* expected_value);

static cJSON* pgagroal_management_json_read_conf_ls(SSL* ssl, int socket);
static int pgagroal_management_json_print_conf_ls(cJSON* json);

static int pgagroal_executable_version_number(char* version, size_t version_size);
static int pgagroal_executable_version_string(char** version_string, int version_number);
static char* pgagroal_executable_name(int command);

static cJSON* pgagroal_json_create_new_command_object(char* command_name, bool success, char* executable_name, char* executable_version);
static cJSON* pgagroal_json_extract_command_output_object(cJSON* json);
static int pgagroal_json_set_command_object_faulty(cJSON* json, char* message, int exit_status);
static const char* pgagroal_json_get_command_object_status(cJSON* json);
static bool pgagroal_json_is_command_name_equals_to(cJSON* json, char* command_name);
static int pgagroal_json_print_and_free_json_object(cJSON* json);
static int pgagroal_json_command_object_exit_status(cJSON* json);

int
pgagroal_management_read_header(int socket, signed char* id, int32_t* slot)
{
   char header[MANAGEMENT_HEADER_SIZE];
   char buf_info[MANAGEMENT_INFO_SIZE];

   if (read_complete(NULL, socket, &header[0], sizeof(header)))
   {
      pgagroal_log_warn("pgagroal_management_read_header: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *id = pgagroal_read_byte(&(header));
   *slot = pgagroal_read_int32(&(header[1]));

   if (read_complete(NULL, socket, &buf_info[0], sizeof(buf_info)))
   {
      pgagroal_log_warn("pgagroal_management_read_header: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   *id = -1;
   *slot = -1;

   return 1;
}

int
pgagroal_management_read_payload(int socket, signed char id, int* payload_i, char** payload_s)
{
   int nr;
   char* s = NULL;
   char buf2[2];
   char buf4[4];
   int size;
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;

   *payload_i = -1;
   *payload_s = NULL;

   switch (id)
   {
      case MANAGEMENT_TRANSFER_CONNECTION:
      case MANAGEMENT_CLIENT_FD:
         memset(&buf2[0], 0, sizeof(buf2));

         iov[0].iov_base = &buf2[0];
         iov[0].iov_len = sizeof(buf2);

         cmptr = calloc(1, CMSG_SPACE(sizeof(int)));
         if (cmptr == NULL)
         {
            goto error;
         }
         cmptr->cmsg_len = CMSG_LEN(sizeof(int));
         cmptr->cmsg_level = SOL_SOCKET;
         cmptr->cmsg_type = SCM_RIGHTS;

         msg.msg_name = NULL;
         msg.msg_namelen = 0;
         msg.msg_iov = iov;
         msg.msg_iovlen = 1;
         msg.msg_control = cmptr;
         msg.msg_controllen = CMSG_SPACE(sizeof(int));
         msg.msg_flags = 0;

         if ((nr = recvmsg(socket, &msg, 0)) < 0)
         {
            goto error;
         }
         else if (nr == 0)
         {
            goto error;
         }

         *payload_i = *(int*)CMSG_DATA(cmptr);

         free(cmptr);
         break;
      case MANAGEMENT_FLUSH:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         *payload_i = pgagroal_read_int32(&buf4);

         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         size = pgagroal_read_int32(&buf4);

         s = calloc(1, size + 1);
         if (s == NULL)
         {
            goto error;
         }
         if (read_complete(NULL, socket, s, size))
         {
            goto error;
         }
         *payload_s = s;
         break;
      case MANAGEMENT_KILL_CONNECTION:
      case MANAGEMENT_CLIENT_DONE:
      case MANAGEMENT_REMOVE_FD:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         *payload_i = pgagroal_read_int32(&buf4);
         break;
      case MANAGEMENT_ENABLEDB:
      case MANAGEMENT_DISABLEDB:
      case MANAGEMENT_CONFIG_GET:
      case MANAGEMENT_GET_PASSWORD:
      case MANAGEMENT_CONFIG_SET:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         *payload_i = pgagroal_read_int32(&buf4);

         s = calloc(1, *payload_i + 1);
         if (s == NULL)
         {
            goto error;
         }
         if (read_complete(NULL, socket, s, *payload_i))
         {
            goto error;
         }
         *payload_s = s;
         break;
      case MANAGEMENT_RESET_SERVER:
      case MANAGEMENT_SWITCH_TO:
         s = calloc(1, MISC_LENGTH);
         if (s == NULL)
         {
            goto error;
         }
         if (read_complete(NULL, socket, s, MISC_LENGTH))
         {
            goto error;
         }
         *payload_s = s;
         break;
      case MANAGEMENT_RETURN_CONNECTION:
      case MANAGEMENT_GRACEFULLY:
      case MANAGEMENT_STOP:
      case MANAGEMENT_CANCEL_SHUTDOWN:
      case MANAGEMENT_STATUS:
      case MANAGEMENT_DETAILS:
      case MANAGEMENT_RESET:
      case MANAGEMENT_RELOAD:
      case MANAGEMENT_CONFIG_LS:
         break;
      default:
         goto error;
         break;
   }

   return 0;

error:

   if (cmptr)
   {
      free(cmptr);
   }

   return 1;
}

int
pgagroal_management_transfer_connection(int32_t slot)
{
   int fd;
   struct main_configuration* config;
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf2[2];

   config = (struct main_configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &fd))
   {
      pgagroal_log_warn("pgagroal_management_transfer_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_TRANSFER_CONNECTION, slot))
   {
      pgagroal_log_warn("pgagroal_management_transfer_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   /* Write file descriptor */
   memset(&buf2[0], 0, sizeof(buf2));

   iov[0].iov_base = &buf2[0];
   iov[0].iov_len = sizeof(buf2);

   cmptr = calloc(1, CMSG_SPACE(sizeof(int)));
   if (cmptr == NULL)
   {
      goto error;
   }

   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));

   msg.msg_name = NULL;
   msg.msg_namelen = 0;
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_SPACE(sizeof(int));
   msg.msg_flags = 0;
   *(int*)CMSG_DATA(cmptr) = config->connections[slot].fd;

   if (sendmsg(fd, &msg, 0) != 2)
   {
      goto error;
   }

   free(cmptr);
   pgagroal_disconnect(fd);

   return 0;

error:
   if (cmptr)
   {
      free(cmptr);
   }
   pgagroal_disconnect(fd);
   pgagroal_kill_connection(slot, NULL);

   return 1;
}

int
pgagroal_management_return_connection(int32_t slot)
{
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &fd))
   {
      pgagroal_log_warn("pgagroal_management_return_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_RETURN_CONNECTION, slot))
   {
      pgagroal_log_warn("pgagroal_management_return_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_disconnect(fd);

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_kill_connection(int32_t slot, int socket)
{
   int fd;
   char buf[4];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &fd))
   {
      pgagroal_log_warn("pgagroal_management_kill_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_KILL_CONNECTION, slot))
   {
      pgagroal_log_warn("pgagroal_management_kill_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, socket);
   if (write_complete(NULL, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_kill_connection: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   pgagroal_disconnect(fd);

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_flush(SSL* ssl, int fd, int32_t mode, char* database)
{
   char buf[4];

   if (write_header(ssl, fd, MANAGEMENT_FLUSH, -1))
   {
      pgagroal_log_warn("pgagroal_management_flush: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, mode);
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, strlen(database));
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, fd, database, strlen(database)))
   {
      pgagroal_log_warn("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_enabledb(SSL* ssl, int fd, char* database)
{
   char buf[4];

   if (write_header(ssl, fd, MANAGEMENT_ENABLEDB, -1))
   {
      pgagroal_log_warn("pgagroal_management_enabledb: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, strlen(database));
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_enabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, fd, database, strlen(database)))
   {
      pgagroal_log_warn("pgagroal_management_enabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_get_password(SSL* ssl, int fd, char* username, char* pass)
{
   char buf[4];
   int* password_length = NULL;
   char password[MAX_PASSWORD_LENGTH];
   char buffer[strlen(username) + 4];

   password_length = (int*)malloc(sizeof(int));
   if (!password_length)
   {
      goto error;
   }

   if (write_header(ssl, fd, MANAGEMENT_GET_PASSWORD, -1))
   {
      pgagroal_log_warn("pgagroal_management_get_password: write-header: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, (int32_t)strlen(username));
   memset(buffer, 0, sizeof(buffer));
   memcpy(buffer, buf, 4);
   memcpy(buffer + 4, username, strlen(username));

   // write username to the management port
   if (write_complete(ssl, fd, buffer, strlen(username) + 4))
   {
      pgagroal_log_warn("pgagroal_management_get_password: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   // Read the Password length
   if (read_complete(ssl, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_get_password: read: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }
   *password_length = pgagroal_read_int32(&buf);

   // Read the Password
   memset(password, 0, sizeof(password));
   if (read_complete(ssl, fd, password, *password_length))
   {
      pgagroal_log_warn("pgagroal_management_get_password: read: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   memcpy(pass, password, *password_length);

   free(password_length);
   return 0;

error:

   free(password_length);
   return 1;
}

int
pgagroal_management_disabledb(SSL* ssl, int fd, char* database)
{
   char buf[4];

   if (write_header(ssl, fd, MANAGEMENT_DISABLEDB, -1))
   {
      pgagroal_log_warn("pgagroal_management_disabledb: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, strlen(database));
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_disabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, fd, database, strlen(database)))
   {
      pgagroal_log_warn("pgagroal_management_disabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_gracefully(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_GRACEFULLY, -1))
   {
      pgagroal_log_warn("pgagroal_management_gracefully: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_stop(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_STOP, -1))
   {
      pgagroal_log_warn("pgagroal_management_stop: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_cancel_shutdown(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_CANCEL_SHUTDOWN, -1))
   {
      pgagroal_log_warn("pgagroal_management_cancel_shutdown: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_status(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_STATUS, -1))
   {
      pgagroal_log_warn("pgagroal_management_status: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_status(SSL* ssl, int socket, char output_format)
{
   cJSON* json = pgagroal_management_json_read_status_details(ssl, socket, false);

   // check we have an answer, note that a faulty answer is still valid to be printed!
   if (!json)
   {
      goto error;
   }

   // print out the command answer
   if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      return pgagroal_json_print_and_free_json_object(json);
   }
   else
   {
      return pgagroal_management_json_print_status_details(json);
   }

error:
   pgagroal_log_warn("pgagroal_management_read_status: command error [%s]",
                     (json == NULL ? "<unknown>" : pgagroal_json_get_command_object_status(json)));
   return 1;
}

/**
 * Utility method that reads the answer from pgagroal about
 * either the 'status' or the 'status details' command.
 * The answer is then wrapped into a JSON object
 * that contains all the information needed to be printed out in either
 * JSON format or text format.
 *
 * @param ssl the SSL file descriptor for the socket
 * @param socket the socket file descriptor
 * @param include_details true if the method has to handle the 'status details' command
 * or false if the answer is related only to the 'status' command
 *
 * @returns the json object, faulty if something goes wrong
 */
static cJSON*
pgagroal_management_json_read_status_details(SSL* ssl, int socket, bool include_details)
{
   char buf[16];
   char disabled[NUMBER_OF_DISABLED][MAX_DATABASE_LENGTH];
   int status;
   int active;
   int total;
   int max;
   int max_connections = 0;
   int limits = 0;
   int servers = 0;
   char header[12 + MAX_NUMBER_OF_CONNECTIONS];
   char buf_info[MANAGEMENT_INFO_SIZE];
   int application;
   int version;

   memset(&buf_info, 0, sizeof(buf_info));
   memset(&buf, 0, sizeof(buf));
   memset(&disabled, 0, sizeof(disabled));
   memset(&header, 0, sizeof(header));

   if (read_complete(ssl, socket, &buf_info[0], sizeof(buf_info)))
   {
      pgagroal_log_warn("pgagroal_management_json_read_status_details: read: %d %s", socket, strerror(errno));
      errno = 0;
      return NULL;
   }

   application = pgagroal_read_int32(&(buf_info[2]));
   version = pgagroal_read_int32(&(buf_info[9]));

   char* version_buf = NULL;

   if (pgagroal_executable_version_string(&version_buf, version))
   {
      return NULL;
   }

   cJSON* json = pgagroal_json_create_new_command_object(include_details ? "status details" :  "status", true, pgagroal_executable_name(application), version_buf);
   cJSON* output = pgagroal_json_extract_command_output_object(json);

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_json_read_status_details: read: %d %s", socket, strerror(errno));
      goto error;
   }

   if (read_complete(ssl, socket, &disabled[0], sizeof(disabled)))
   {
      pgagroal_log_warn("pgagroal_management_json_read_status_details: read: %d %s", socket, strerror(errno));
      goto error;
   }

   status = pgagroal_read_int32(&buf);
   active = pgagroal_read_int32(&(buf[4]));
   total = pgagroal_read_int32(&(buf[8]));
   max = pgagroal_read_int32(&(buf[12]));

   // status information
   cJSON* status_json = cJSON_CreateObject();
   cJSON_AddStringToObject(status_json, "message", (status == 1 ? "Running" : "Graceful shutdown"));
   cJSON_AddNumberToObject(status_json, "status", status);
   cJSON_AddItemToObject(output, "status", status_json);

   // define all the information about connections
   cJSON* connections = cJSON_CreateObject();
   cJSON_AddNumberToObject(connections, "active", active);
   cJSON_AddNumberToObject(connections, "total", total);
   cJSON_AddNumberToObject(connections, "max", max);
   cJSON_AddItemToObject(output, "connections", connections);

   // define all the information about disabled databases
   cJSON* databases = cJSON_CreateObject();
   cJSON* databases_array = cJSON_CreateArray();

   int counter = 0;

   for (int i = 0; i < NUMBER_OF_DISABLED; i++)
   {
      if (strcmp(disabled[i], ""))
      {
         if (!strcmp(disabled[i], "*"))
         {
            cJSON_AddItemToArray(databases_array, cJSON_CreateString("ALL"));
            counter = -1;
         }
         else
         {
            cJSON_AddItemToArray(databases_array, cJSON_CreateString(disabled[i]));
            counter++;
         }
      }
   }

   cJSON* disabled_databases = cJSON_CreateObject();
   cJSON_AddNumberToObject(disabled_databases, "count", counter);
   cJSON_AddStringToObject(disabled_databases, "state", "disabled");
   cJSON_AddItemToObject(disabled_databases, JSON_TAG_ARRAY_NAME, databases_array);
   cJSON_AddItemToObject(databases, "disabled", disabled_databases);
   cJSON_AddItemToObject(output, "databases", databases);

   // the 'status' command ends here
   if (!include_details)
   {
      goto end;
   }

   /*********** 'status details ************/

   memset(&header, 0, sizeof(header));

   if (read_complete(ssl, socket, &header[0], sizeof(header)))
   {
      goto error;
   }

   // quantity informations
   max_connections = pgagroal_read_int32(&header);
   limits = pgagroal_read_int32(&(header[4]));
   servers = pgagroal_read_int32(&(header[8]));

   cJSON* json_servers = cJSON_CreateObject();
   cJSON* json_servers_array = cJSON_CreateArray();
   cJSON_AddItemToObject(output, "servers", json_servers);
   cJSON_AddNumberToObject(json_servers, "count", servers);

   // details about the servers
   for (int i = 0; i < servers; i++)
   {
      char server[5 + MISC_LENGTH + MISC_LENGTH];

      memset(&server, 0, sizeof(server));

      if (read_complete(ssl, socket, &server[0], sizeof(server)))
      {
         goto error;
      }

      cJSON* current_server_json = cJSON_CreateObject();
      cJSON_AddStringToObject(current_server_json, "server", pgagroal_read_string(&(server[0])));
      cJSON_AddStringToObject(current_server_json, "host", pgagroal_read_string(&(server[MISC_LENGTH])));
      cJSON_AddNumberToObject(current_server_json, "port", pgagroal_read_int32(&(server[MISC_LENGTH + MISC_LENGTH])));
      cJSON_AddStringToObject(current_server_json, "state", pgagroal_server_state_as_string(pgagroal_read_byte(&(server[MISC_LENGTH + MISC_LENGTH + 4]))));

      cJSON_AddItemToArray(json_servers_array, current_server_json);
   }

   cJSON_AddItemToObject(json_servers, JSON_TAG_ARRAY_NAME, json_servers_array);

   // details about the limits
   cJSON* json_limits = cJSON_CreateObject();
   cJSON* json_limits_array = cJSON_CreateArray();
   cJSON_AddItemToObject(json_limits, JSON_TAG_ARRAY_NAME, json_limits_array);
   cJSON_AddItemToObject(output, "limits", json_limits);
   cJSON_AddNumberToObject(json_limits, "count", limits);

   for (int i = 0; i < limits; i++)
   {
      char limit[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH];
      memset(&limit, 0, sizeof(limit));

      if (read_complete(ssl, socket, &limit[0], sizeof(limit)))
      {
         goto error;
      }

      cJSON* current_limit_json = cJSON_CreateObject();

      cJSON_AddStringToObject(current_limit_json, "database", pgagroal_read_string(&(limit[16])));
      cJSON_AddStringToObject(current_limit_json, "username", pgagroal_read_string(&(limit[16 + MAX_DATABASE_LENGTH])));

      cJSON* current_connections = cJSON_CreateObject();

      cJSON_AddNumberToObject(current_connections, "active", pgagroal_read_int32(&(limit)));
      cJSON_AddNumberToObject(current_connections, "max", pgagroal_read_int32(&(limit[4])));
      cJSON_AddNumberToObject(current_connections, "initial", pgagroal_read_int32(&(limit[8])));
      cJSON_AddNumberToObject(current_connections, "min", pgagroal_read_int32(&(limit[12])));

      cJSON_AddItemToObject(current_limit_json, "connections", current_connections);
      cJSON_AddItemToArray(json_limits_array, current_limit_json);

   }

   // max connections details (note that the connections json object has been created
   // as part of the status output)
   cJSON* connections_array = cJSON_CreateArray();
   cJSON_AddItemToObject(connections, JSON_TAG_ARRAY_NAME, connections_array);

   for (int i = 0; i < max_connections; i++)
   {
      char details[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH + MAX_APPLICATION_NAME];
      signed char state;
      long time;
      time_t t;
      char ts[20] = {0};
      int pid;
      char p[10] = {0};
      int fd;
      char f[10] = {0};

      memset(&details, 0, sizeof(details));

      if (read_complete(ssl, socket, &details[0], sizeof(details)))
      {

         goto error;
      }

      state = (signed char)header[12 + i];
      time = pgagroal_read_long(&(details[0]));
      pid = pgagroal_read_int32(&(details[8]));
      fd = pgagroal_read_int32(&(details[12]));

      t = time;
      strftime(ts, 20, "%Y-%m-%d %H:%M:%S", localtime(&t));

      sprintf(p, "%d", pid);
      sprintf(f, "%d", fd);

      cJSON* current_connection_json = cJSON_CreateObject();

      cJSON_AddNumberToObject(current_connection_json, "number", i + 1);
      cJSON_AddStringToObject(current_connection_json, "state", pgagroal_connection_state_as_string(state));
      cJSON_AddStringToObject(current_connection_json, "time", time > 0 ? ts : "");
      cJSON_AddStringToObject(current_connection_json, "pid", pid > 0 ? p : "");
      cJSON_AddStringToObject(current_connection_json, "fd", fd > 0 ? f : "");
      cJSON_AddStringToObject(current_connection_json, "database", pgagroal_read_string(&(details[16])));
      cJSON_AddStringToObject(current_connection_json, "user", pgagroal_read_string(&(details[16 + MAX_DATABASE_LENGTH])));
      cJSON_AddStringToObject(current_connection_json, "detail", pgagroal_read_string(&(details[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH])));

      cJSON_AddItemToArray(connections_array, current_connection_json);

   }

end:
   free(version_buf);
   return json;

error:
   // set the json object as faulty and erase the errno
   pgagroal_json_set_command_object_faulty(json, strerror(errno), errno);
   errno = 0;
   return json;
}

int
pgagroal_management_write_status(int socket, bool graceful)
{
   char buf[16];
   char buf_info[MANAGEMENT_INFO_SIZE];

   int active;
   int total;
   struct main_configuration* config;

   if (write_info(buf_info, PGAGROAL_EXECUTABLE, 0))
   {
      goto error;
   }

   if (write_complete(NULL, socket, &buf_info, sizeof(buf_info)))
   {
      pgagroal_log_warn("pgagroal_management_write_status: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   memset(&buf, 0, sizeof(buf));
   active = 0;
   total = 0;

   config = (struct main_configuration*)shmem;

   if (!graceful)
   {
      pgagroal_write_int32(&buf, 1);
   }
   else
   {
      pgagroal_write_int32(&buf, 2);
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);
      switch (state)
      {
         case STATE_IN_USE:
         case STATE_GRACEFULLY:
            active++;
         case STATE_INIT:
         case STATE_FREE:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_MAX_CONNECTION_AGE:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            total++;
            break;
         default:
            break;
      }
   }

   pgagroal_write_int32(&(buf[4]), active);
   pgagroal_write_int32(&(buf[8]), total);
   pgagroal_write_int32(&(buf[12]), config->max_connections);

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_write_status: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(NULL, socket, &config->disabled, sizeof(config->disabled)))
   {
      pgagroal_log_warn("pgagroal_management_write_status: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_details(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_DETAILS, -1))
   {
      pgagroal_log_warn("pgagroal_management_details: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_details(SSL* ssl, int socket, char output_format)
{
   cJSON* json = pgagroal_management_json_read_status_details(ssl, socket, true);

   // check we have an answer, note that a faulty answer is still worth to be printed
   if (!json)
   {
      goto error;
   }

   // print out the command answer
   if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      return pgagroal_json_print_and_free_json_object(json);
   }
   else
   {
      return pgagroal_management_json_print_status_details(json);
   }

error:
   pgagroal_log_warn("pgagroal_management_read_details: command error [%s]",
                     (json == NULL ? "<unknown>" : pgagroal_json_get_command_object_status(json)));

   return 1;
}

int
pgagroal_management_write_details(int socket)
{
   struct main_configuration* config;
   int offset = 12;
   char header[offset + MAX_NUMBER_OF_CONNECTIONS];

   config = (struct main_configuration*)shmem;

   memset(&header, 0, sizeof(header));

   pgagroal_write_int32(header, config->max_connections);
   pgagroal_write_int32(header + 4, config->number_of_limits);
   pgagroal_write_int32(header + 8, config->number_of_servers);

   for (int i = 0; i < config->max_connections; i++)
   {
      signed char state = atomic_load(&config->states[i]);
      header[offset + i] = (char)state;
   }

   if (write_complete(NULL, socket, header, sizeof(header)))
   {
      pgagroal_log_warn("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      char server[5 + MISC_LENGTH + MISC_LENGTH];

      memset(&server, 0, sizeof(server));

      pgagroal_write_string(server, config->servers[i].name);
      pgagroal_write_string(server + MISC_LENGTH, config->servers[i].host);
      pgagroal_write_int32(server + MISC_LENGTH + MISC_LENGTH, config->servers[i].port);
      pgagroal_write_byte(server + MISC_LENGTH + MISC_LENGTH + 4, atomic_load(&config->servers[i].state));

      if (write_complete(NULL, socket, server, sizeof(server)))
      {
         pgagroal_log_warn("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   for (int i = 0; i < config->number_of_limits; i++)
   {
      char limit[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH];

      memset(&limit, 0, sizeof(limit));

      pgagroal_write_int32(limit, atomic_load(&config->limits[i].active_connections));
      pgagroal_write_int32(limit + 4, config->limits[i].max_size);
      pgagroal_write_int32(limit + 8, config->limits[i].initial_size);
      pgagroal_write_int32(limit + 12, config->limits[i].min_size);
      pgagroal_write_string(limit + 16, config->limits[i].database);
      pgagroal_write_string(limit + 16 + MAX_DATABASE_LENGTH, config->limits[i].username);

      if (write_complete(NULL, socket, &limit, sizeof(limit)))
      {
         pgagroal_log_warn("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      char details[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH + MAX_APPLICATION_NAME];

      memset(&details, 0, sizeof(details));

      pgagroal_write_long(details, (long)config->connections[i].start_time);
      pgagroal_write_long(details, (long)config->connections[i].timestamp);
      pgagroal_write_int32(details + 8, (int)config->connections[i].pid);
      pgagroal_write_int32(details + 12, (int)config->connections[i].fd);

      pgagroal_write_string(details + 16, config->connections[i].database);
      pgagroal_write_string(details + 16 + MAX_DATABASE_LENGTH, config->connections[i].username);
      pgagroal_write_string(details + 16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH, config->connections[i].appname);

      if (write_complete(NULL, socket, &details, sizeof(details)))
      {
         pgagroal_log_warn("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_isalive(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_ISALIVE, -1))
   {
      pgagroal_log_warn("pgagroal_management_isalive: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_isalive(SSL* ssl, int socket, int* status, char output_format)
{
   char buf[MANAGEMENT_INFO_SIZE + 4];
   int application;
   int version;

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_read_isalive: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   application = pgagroal_read_int32(&(buf[2]));
   version = pgagroal_read_int32(&(buf[9]));

   *status = pgagroal_read_int32(&buf[MANAGEMENT_INFO_SIZE]);

   // do I need to provide JSON output?
   if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      char* version_buf = NULL;

      if (pgagroal_executable_version_string(&version_buf, version))
      {
         goto error;
      }

      cJSON* json = pgagroal_json_create_new_command_object("ping", true, pgagroal_executable_name(application), version_buf);
      cJSON* output = pgagroal_json_extract_command_output_object(json);

      cJSON_AddNumberToObject(output, "status", *status);

      if (*status == PING_STATUS_RUNNING)
      {
         cJSON_AddStringToObject(output, "message", "running");
      }
      else if (*status == PING_STATUS_SHUTDOWN_GRACEFULLY)
      {
         cJSON_AddStringToObject(output, "message", "shutdown gracefully");
      }
      else
      {
         cJSON_AddStringToObject(output, "message", "unknown");
      }

      return pgagroal_json_print_and_free_json_object(json);

   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_isalive(int socket, bool gracefully)
{
   char buf[4];
   char buf_info[MANAGEMENT_INFO_SIZE];

   if (write_info(buf_info, PGAGROAL_EXECUTABLE, 0))
   {
      goto error;
   }

   if (write_complete(NULL, socket, &buf_info, sizeof(buf_info)))
   {
      pgagroal_log_warn("pgagroal_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   memset(&buf, 0, sizeof(buf));

   if (!gracefully)
   {
      pgagroal_write_int32(buf, PING_STATUS_RUNNING);
   }
   else
   {
      pgagroal_write_int32(buf, PING_STATUS_SHUTDOWN_GRACEFULLY);
   }

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_get_password(SSL* ssl, int socket, char* password)
{
   char buffer[MAX_PASSWORD_LENGTH + 4]; // first 4 bytes contains the length of the password
   memset(buffer, 0, sizeof(buffer));

   pgagroal_write_int32(&buffer, strlen(password));
   memcpy(buffer + 4, password, strlen(password));

   if (write_complete(ssl, socket, buffer, strlen(password) + 4))
   {
      pgagroal_log_warn("pgagroal_management_write_get_password: write: %d %s\n", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_reset(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_RESET, -1))
   {
      pgagroal_log_warn("pgagroal_management_reset: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_reset_server(SSL* ssl, int fd, char* server)
{
   char name[MISC_LENGTH];

   if (write_header(ssl, fd, MANAGEMENT_RESET_SERVER, -1))
   {
      pgagroal_log_warn("pgagroal_management_reset_server: write: %d", fd);
      errno = 0;
      goto error;
   }

   memset(&name[0], 0, MISC_LENGTH);
   memcpy(&name[0], server, strlen(server));

   if (write_complete(ssl, fd, &name[0], sizeof(name)))
   {
      pgagroal_log_warn("pgagroal_management_reset_server_: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_client_done(pid_t pid)
{
   char buf[4];
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &fd))
   {
      pgagroal_log_warn("pgagroal_management_client_done: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_CLIENT_DONE, -1))
   {
      pgagroal_log_warn("pgagroal_management_client_done: write: %d", fd);
      errno = 0;
      goto error;
   }

   memset(&buf, 0, sizeof(buf));
   pgagroal_write_int32(buf, pid);

   if (write_complete(NULL, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_client_done: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   pgagroal_disconnect(fd);

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_client_fd(int32_t slot, pid_t pid)
{
   char p[MISC_LENGTH];
   int fd;
   struct main_configuration* config;
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf[2]; /* send_fd()/recv_fd() 2-byte protocol */

   config = (struct main_configuration*)shmem;

   memset(&p, 0, sizeof(p));
   snprintf(&p[0], sizeof(p), ".s.%d", pid);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &p[0], &fd))
   {
      pgagroal_log_debug("pgagroal_management_client_fd: connect: %d", fd);
      errno = 0;
      goto unavailable;
   }

   if (write_header(NULL, fd, MANAGEMENT_CLIENT_FD, slot))
   {
      pgagroal_log_warn("pgagroal_management_client_fd: write: %d", fd);
      errno = 0;
      goto error;
   }

   /* Write file descriptor */
   iov[0].iov_base = buf;
   iov[0].iov_len = 2;
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_name = NULL;
   msg.msg_namelen = 0;

   cmptr = malloc(CMSG_LEN(sizeof(int)));
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_LEN(sizeof(int));
   *(int*)CMSG_DATA(cmptr) = config->connections[slot].fd;
   buf[1] = 0; /* zero status means OK */
   buf[0] = 0; /* null byte flag to recv_fd() */

   if (sendmsg(fd, &msg, 0) != 2)
   {
      goto error;
   }

   free(cmptr);
   pgagroal_disconnect(fd);

   return 0;

unavailable:
   free(cmptr);
   pgagroal_disconnect(fd);

   return 1;

error:
   free(cmptr);
   pgagroal_disconnect(fd);
   pgagroal_kill_connection(slot, NULL);

   return 1;
}

int
pgagroal_management_switch_to(SSL* ssl, int fd, char* server)
{
   char name[MISC_LENGTH];

   if (write_header(ssl, fd, MANAGEMENT_SWITCH_TO, -1))
   {
      pgagroal_log_warn("pgagroal_management_switch_to: write: %d", fd);
      errno = 0;
      goto error;
   }

   memset(&name[0], 0, MISC_LENGTH);
   memcpy(&name[0], server, strlen(server));

   if (write_complete(ssl, fd, &name[0], sizeof(name)))
   {
      pgagroal_log_warn("pgagroal_management_switch_to: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_reload(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_RELOAD, -1))
   {
      pgagroal_log_warn("pgagroal_management_reload: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_remove_fd(int32_t slot, int socket, pid_t pid)
{
   char p[MISC_LENGTH];
   int fd;
   char buf[4];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (atomic_load(&config->states[slot]) == STATE_NOTINIT)
   {
      return 0;
   }

   memset(&p, 0, sizeof(p));
   snprintf(&p[0], sizeof(p), ".s.%d", pid);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &p[0], &fd))
   {
      pgagroal_log_debug("pgagroal_management_remove_fd: slot %d state %d database %s user %s socket %d pid %d connect: %d",
                         slot, atomic_load(&config->states[slot]),
                         config->connections[slot].database, config->connections[slot].username, socket, pid, fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_REMOVE_FD, slot))
   {
      pgagroal_log_warn("pgagroal_management_remove_fd: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, socket);
   if (write_complete(NULL, fd, &buf, sizeof(buf)))
   {
      pgagroal_log_warn("pgagroal_management_remove_fd: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   pgagroal_disconnect(fd);

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r < needs)
   {
      SLEEP(10000000L)

      pgagroal_log_trace("Got: %ld, needs: %ld", r, needs);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgagroal_log_debug("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgagroal_log_debug("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgagroal_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgagroal_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}

/**
 * Function to write sender application name and its version
 * into header socket.
 *
 * @param header: the header to write sender application name and its version into
 * @param command: the server application name
 * @param offset: the offset at which info has to be written
 * @return 0 on success
 */
static int
write_info(char* buffer, int command, int offset)
{
   int exec_version = pgagroal_executable_version_number(PGAGROAL_VERSION, sizeof(PGAGROAL_VERSION));
   if (exec_version == 1)
   {
      return 1;
   }

   char* exec_name = pgagroal_executable_name(command);
   if (exec_name == NULL)
   {
      return 1;
   }

   struct pgagroal_version_info info = {
      .command = command,
      .version = exec_version,
   };

   memcpy(&(info.s), S, sizeof(info.s));
   memcpy(&(info.v), V, sizeof(info.v));

   memcpy(&(buffer[offset]), info.s, sizeof(info.s));
   pgagroal_write_int32(&(buffer[offset + 2]), info.command);

   memcpy(&(buffer[offset + 6]), info.v, sizeof(info.v));
   pgagroal_write_int32(&(buffer[offset + 9]), info.version);

   pgagroal_log_debug("%s version %d", exec_name, exec_version);

   return 0;
}

/*
 * Utility function to convert PGAGROAL_VERSION into a number.
 * The major version is represented by a single digit.
 * For minor and patch, a leading 0 is added if they are single digits.
 */
static int
pgagroal_executable_version_number(char* version, size_t version_size)
{
   int major;
   int minor;
   int patch;

   long val;

   char* del = ".";
   char* endptr;

   if (version == NULL)
   {
      version = PGAGROAL_VERSION;
      version_size = sizeof(version);
   }

   char buf[version_size];

   memcpy(buf, version, sizeof(buf));

   char* token = strtok(buf, del);
   val = strtol(token, &endptr, 10);
   if (errno == ERANGE || val <= LONG_MIN || val >= LONG_MAX)
   {
      goto error;
   }
   major = (int)val;

   token = strtok(NULL, del);
   val = strtol(token, &endptr, 10);
   if (errno == ERANGE || val <= LONG_MIN || val >= LONG_MAX)
   {
      goto error;
   }
   minor = (int)val;

   token = strtok(NULL, del);
   val = strtol(token, &endptr, 10);
   if (errno == ERANGE || val <= LONG_MIN || val >= LONG_MAX)
   {
      goto error;
   }
   patch = (int)val;

   int version_number = (major % 10) * 10000 + (minor / 10) * 1000 + (minor % 10) * 100 + patch;

   if (version_number < INT_MIN || version_number > INT_MAX || version_number < 10700)
   {
      goto error;
   }

   return version_number;

error:
   pgagroal_log_debug("pgagroal_get_executable_number got overflowed or suspicious value: %s %s", version, strerror(errno));
   errno = 0;
   return 1;
}

/*
 * Utility function to convert back version_number into a string.
 */
static int
pgagroal_executable_version_string(char** version_string, int version_number)
{
   char* v = NULL;
   int major = version_number / 10000;
   int minor = (version_number / 100) % 100;
   int patch = version_number % 100;

   v = pgagroal_append_int(v, major);
   v = pgagroal_append(v, ".");
   v = pgagroal_append_int(v, minor);
   v = pgagroal_append(v, ".");
   v = pgagroal_append_int(v, patch);

   *version_string = v;

   return 0;
}

/*
 * Utility function to convert command into a string.
 */
static char*
pgagroal_executable_name(int command)
{
   switch (command)
   {
      case PGAGROAL_EXECUTABLE:
         return "pgagroal";
      case PGAGROAL_EXECUTABLE_CLI:
         return "pgagroal-cli";
      case PGAGROAL_EXECUTABLE_VAULT:
         return "pgagroal-vault";
      default:
         pgagroal_log_debug("pgagroal_get_command_name got unexpected value: %d", command);
         return NULL;
   }
}

static int
write_header(SSL* ssl, int fd, signed char type, int slot)
{
   char header[MANAGEMENT_HEADER_SIZE + MANAGEMENT_INFO_SIZE];

   pgagroal_write_byte(&(header), type);
   pgagroal_write_int32(&(header[1]), slot);

   if (write_info(header, PGAGROAL_EXECUTABLE_CLI, MANAGEMENT_HEADER_SIZE))
   {
      return 1;
   }

   return write_complete(ssl, fd, &(header), sizeof(header));
}

int
pgagroal_management_config_get(SSL* ssl, int socket, char* config_key)
{
   char buf[4];
   size_t size;

   // security check: avoid writing something null or with too much stuff!
   if (!config_key || !strlen(config_key))
   {
      pgagroal_log_debug("pgagroal_management_config_get: no key specified");
      goto error;
   }

   size = strlen(config_key) + 1;
   if (size > MISC_LENGTH)
   {
      pgagroal_log_debug("pgagroal_management_config_get: key <%s> too big (%d bytes)", config_key, size);
      goto error;
   }

   // send the header for this command
   if (write_header(ssl, socket, MANAGEMENT_CONFIG_GET, -1))
   {
      pgagroal_log_debug("pgagroal_management_config_get: write error on header for key <%s> on socket %d", config_key, socket);
      goto error;
   }

   // send the size of the payload
   memset(&buf, 0, sizeof(buf));
   pgagroal_write_int32(&buf, size);
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgagroal_log_debug("pgagroal_management_config_get: write error for the size of the payload (%d bytes for <%s>, socket %d): %s",
                         size,
                         config_key,
                         socket,
                         strerror(errno));
      goto error;
   }

   // send the effective payload, i.e., the configuration parameter name to get
   memset(&buf, 0, sizeof(buf));

   if (write_complete(ssl, socket, config_key, size))
   {
      pgagroal_log_debug("pgagroal_management_config_get: write error sending the configuration name <%s> over socket %d: %s", config_key, socket, strerror(errno));
      goto error;
   }

   return 0;

error:
   errno = 0;
   return 1;
}

int
pgagroal_management_write_config_get(int socket, char* config_key)
{
   char data[MISC_LENGTH];
   char buf[4];
   char buf_info[MANAGEMENT_INFO_SIZE];

   size_t size;

   if (write_info(buf_info, PGAGROAL_EXECUTABLE, 0))
   {
      goto error;
   }

   if (write_complete(NULL, socket, buf_info, sizeof(buf_info)))
   {
      pgagroal_log_debug("pgagroal_management_config_get: write: %d %s", socket, strerror(errno));
      goto error;
   }

   if (!config_key || !strlen(config_key))
   {
      pgagroal_log_debug("pgagroal_management_write_config_get: no key specified");
      goto error;
   }

   size = strlen(config_key) + 1;
   if (size > MISC_LENGTH)
   {
      pgagroal_log_debug("pgagroal_management_write_config_get: key <%s> too big (%d bytes)", config_key, size);
      goto error;
   }

   memset(&data, 0, sizeof(data));

   if (pgagroal_write_config_value(&data[0], config_key, sizeof(data)))
   {
      pgagroal_log_debug("pgagroal_management_write_config_get: unknwon configuration key <%s>", config_key);
      // leave the payload empty, so a zero filled payload will be sent
   }

   // send the size of the payload
   memset(&buf, 0, sizeof(buf));
   size = strlen(data) + 1;
   pgagroal_write_int32(&buf, size);
   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgagroal_log_debug("pgagroal_management_write_config_get: write error for the size of the payload <%s> (%d bytes for <%s>, socket %d): %s",
                         data,
                         size,
                         config_key,
                         socket,
                         strerror(errno));
      goto error;
   }

   if (write_complete(NULL, socket, data, size))
   {
      pgagroal_log_debug("pgagroal_management_write_config_get (%s): write: %d %s", config_key, socket, strerror(errno));
      goto error;
   }

   return 0;

error:
   errno = 0;
   return 1;

}

/**
 * Utility method to wrap the answer about a configuration setting
 * into a JSON object.
 *
 * @param socket the socket from which reading the data from
 * @param config_key the key requested, used only to populate the json
 * @param expected_value the config value expected in the case of a `config set`.
 * If the expetced_value is not null, the function checks if the obtained config value and
 * the expected one are equal, and in case are not set the JSON object as faulty.
 *
 * @return the JSON object
 */
static cJSON*
pgagroal_managment_json_read_config_get(int socket, char* config_key, char* expected_value)
{
   char buf_info[MANAGEMENT_INFO_SIZE];
   int version;
   int application;

   if (read_complete(NULL, socket, &buf_info[0], sizeof(buf_info)))
   {
      pgagroal_log_debug("pgagroal_management_write_config_get (%s): write: %d %s", config_key, socket, strerror(errno));
      errno = 0;
      return NULL;
   }

   int size = MISC_LENGTH;
   char* buffer = NULL;
   bool is_config_set = false;

   buffer = calloc(1, size);
   if (buffer == NULL)
   {
      goto error;
   }

   if (pgagroal_management_read_payload(socket, MANAGEMENT_CONFIG_GET, &size, &buffer))
   {
      goto error;
   }

   // is this the answer from a 'conf set' command ?
   is_config_set = (expected_value && strlen(expected_value) > 0);

   application = pgagroal_read_int32(&(buf_info[2]));
   version = pgagroal_read_int32(&(buf_info[9]));

   char* version_buf = NULL;

   if (pgagroal_executable_version_string(&version_buf, version))
   {
      goto error;
   }

   cJSON* json = pgagroal_json_create_new_command_object(is_config_set ? "conf set" :  "conf get", true, pgagroal_executable_name(application), version_buf);
   cJSON* output = pgagroal_json_extract_command_output_object(json);

   cJSON_AddStringToObject(output, "key", config_key);
   cJSON_AddStringToObject(output, "value", buffer);

   if (is_config_set)
   {
      cJSON_AddStringToObject(output, "expected", expected_value);

      // if the expected value is not what we get, this means there is an error
      // (e.g., cannot apply the config set)
      if (strncmp(buffer, expected_value, size))
      {
         pgagroal_json_set_command_object_faulty(json, "Current and expected values are different", EXIT_STATUS_DATA_ERROR);
      }
   }

   free(buffer);
   return json;
error:
   free(buffer);
   return NULL;
}

int
pgagroal_management_read_config_get(int socket, char* config_key, char* expected_value, bool verbose, char output_format)
{
   cJSON* json = pgagroal_managment_json_read_config_get(socket, config_key, expected_value);
   int status = EXIT_STATUS_OK;

   if (!json)
   {
      goto error;
   }

   // extract the command status
   status = pgagroal_json_command_object_exit_status(json);

   if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      pgagroal_json_print_and_free_json_object(json);
      json = NULL;
      goto end;
   }

   // if here, print out in text format
   cJSON* output = pgagroal_json_extract_command_output_object(json);
   cJSON* value = cJSON_GetObjectItemCaseSensitive(output, "value");
   cJSON* key = cJSON_GetObjectItemCaseSensitive(output, "key");
   if (verbose)
   {
      printf("%s = %s\n", key->valuestring, value->valuestring);
   }
   else
   {
      printf("%s\n", value->valuestring);
   }

   goto end;

error:

   pgagroal_log_warn("pgagroal_management_read_config_get : error retrieving configuration for <%s> : %s", config_key, strerror(errno));
   errno = 0;
   status = EXIT_STATUS_DATA_ERROR;
end:
   if (json)
   {
      cJSON_Delete(json);
   }

   return status;
}

int
pgagroal_management_config_set(SSL* ssl, int socket, char* config_key, char* config_value)
{
   char buf[4];
   size_t size;

   // security check: avoid writing something null or with too much stuff!
   if (!config_key || !strlen(config_key) || !config_value || !strlen(config_value))
   {
      pgagroal_log_debug("pgagroal_management_config_set: no key or value specified");
      goto error;
   }

   if (strlen(config_key) > MISC_LENGTH - 1 || strlen(config_value) > MISC_LENGTH - 1)
   {
      pgagroal_log_debug("pgagroal_management_config_set: key <%s> or value <%s> too big (max %d bytes)", config_key, config_value, MISC_LENGTH);
      goto error;
   }

   // send the header for this command
   if (write_header(ssl, socket, MANAGEMENT_CONFIG_SET, -1))
   {
      pgagroal_log_debug("pgagroal_management_config_set: write error on header for key <%s> on socket %d", config_key, socket);
      goto error;
   }

   /*
    * send a message with the size of the key, the key
    * then the size of the value and the value
    */

   // send the size of the payload for the config key
   memset(&buf, 0, sizeof(buf));
   size = strlen(config_key) + 1;
   pgagroal_write_int32(&buf, size);
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgagroal_log_debug("pgagroal_management_config_set: write error for the size of the payload (%d bytes for <%s>, socket %d): %s",
                         size,
                         config_key,
                         socket,
                         strerror(errno));
      goto error;
   }

   // send the effective payload, i.e., the configuration parameter name to get
   memset(&buf, 0, sizeof(buf));

   if (write_complete(ssl, socket, config_key, size))
   {
      pgagroal_log_debug("pgagroal_management_config_set: write error sending the configuration name <%s> over socket %d: %s", config_key, socket, strerror(errno));
      goto error;
   }

   // send the size of the payload for the config value
   memset(&buf, 0, sizeof(buf));
   size = strlen(config_value) + 1;
   pgagroal_write_int32(&buf, size);
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgagroal_log_debug("pgagroal_management_config_set: write error for the size of the payload (%d bytes for <%s>, socket %d): %s",
                         size,
                         config_value,
                         socket,
                         strerror(errno));
      goto error;
   }

   // send the effective payload, i.e., the configuration value to set
   memset(&buf, 0, sizeof(buf));

   if (write_complete(ssl, socket, config_value, size))
   {
      pgagroal_log_warn("pgagroal_management_config_set: write error sending the configuration value <%s> over socket %d: %s", config_value, socket, strerror(errno));
      goto error;
   }

   return 0;

error:
   errno = 0;
   return 1;
}

int
pgagroal_management_write_config_set(int socket, char* config_key, char* config_value)
{
   if (!config_key || !strlen(config_key) || !config_value || !strlen(config_value))
   {
      pgagroal_log_warn("pgagroal_management_write_config_set: no key or value specified");
      goto error;
   }

   if (strlen(config_key) > MISC_LENGTH - 1 || strlen(config_value) > MISC_LENGTH - 1)
   {
      pgagroal_log_warn("pgagroal_management_write_config_set: key <%s> or value <%s> too big (max %d bytes)", config_key, config_value, MISC_LENGTH);
      goto error;
   }

   pgagroal_log_debug("pgagroal_management_write_config_set: trying to set <%s> to <%s>", config_key, config_value);

   // do set the configuration value
   if (pgagroal_apply_configuration(config_key, config_value))
   {
      pgagroal_log_debug("pgagroal_management_write_config_set: unable to apply changes to <%s> -> <%s>", config_key, config_value);
   }

   // query back the status of the parameter
   // and send it over the socket
   return pgagroal_management_write_config_get(socket, config_key);

error:
   errno = 0;
   return 1;

}

int
pgagroal_management_conf_ls(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_CONFIG_LS, -1))
   {
      pgagroal_log_warn("pgagroal_management_conf_ls: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_conf_ls(SSL* ssl, int socket, char output_format)
{

   // get the JSON output
   cJSON* json = pgagroal_management_json_read_conf_ls(ssl, socket);

   // check we have an answer and it is not an error
   if (!json)
   {
      goto error;
   }

   // print out the command answer
   if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      return pgagroal_json_print_and_free_json_object(json);
   }
   else
   {
      return pgagroal_management_json_print_conf_ls(json);
   }

error:
   pgagroal_log_warn("pgagroal_management_read_conf_ls: read: %d %s", socket, strerror(errno));
   errno = 0;

   return 1;
}

int
pgagroal_management_write_conf_ls(int socket)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   char buf_info[MANAGEMENT_INFO_SIZE];
   if (write_info(buf_info, PGAGROAL_EXECUTABLE, 0))
   {
      goto error;
   }

   if (write_complete(NULL, socket, &buf_info, sizeof(buf_info)))
   {
      goto error;
   }

   if (pgagroal_management_write_conf_ls_detail(socket, config->common.configuration_path))
   {
      goto error;
   }

   if (pgagroal_management_write_conf_ls_detail(socket, config->hba_path))
   {
      goto error;
   }

   if (pgagroal_management_write_conf_ls_detail(socket, config->limit_path))
   {
      goto error;
   }

   // 4
   if (pgagroal_management_write_conf_ls_detail(socket, config->frontend_users_path))
   {
      goto error;
   }
   //5
   if (pgagroal_management_write_conf_ls_detail(socket, config->admins_path))
   {
      goto error;
   }
   //6
   if (pgagroal_management_write_conf_ls_detail(socket, config->superuser_path))
   {
      goto error;
   }
   // 7
   if (pgagroal_management_write_conf_ls_detail(socket, config->users_path))
   {
      goto error;
   }

   return 0;

error:
   pgagroal_log_debug("pgagroal_management_write_conf_ls: error writing out file paths");
   return 1;
}

/**
 * Utility function to write a single configuration path to the socket.
 *
 * @param socket the file descriptor of the open socket
 * @param what the pointer to the path to send out on the socket. It cannot
 * exceed in size MAX_PATH - 1.
 * @returns 0 on success
 */
static int
pgagroal_management_write_conf_ls_detail(int socket, char* what)
{
   char buf[4];
   size_t size = 0;
   char data[MAX_PATH];

   if (what && strlen(what) > MAX_PATH)
   {
      goto error;
   }

   memset(&buf, 0, sizeof(buf));
   memset(&data, 0, sizeof(data));

   size = what ? strlen(what) + 1 : 0;
   if (size > MAX_PATH)
   {
      errno = EMSGSIZE;
      goto error;
   }

   pgagroal_write_int32(&buf, size);

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      goto error;
   }

   memcpy(&data[0], what, size);
   if (write_complete(NULL, socket, data, size))
   {
      goto error;
   }

   pgagroal_log_trace("pgagroal_management_write_conf_ls_deail: writing <%s> with %d bytes", what, size);
   return 0;

error:
   pgagroal_log_debug("pgagroal_management_write_conf_ls_detail: error %d %s", errno, strerror(errno));
   errno = 0;
   return 1;
}

/**
 * Utility function to read back from the socket a configuration path.
 *
 * It does zero fill the buffer pointed by its argument, so
 * it is safe to call this function with a prefilled buffer, but its content
 * will be lost.
 *
 * The buffer will be considered able to store MAX_PATH bytes.
 *
 * @param socket the file descriptor of the open socket
 * @param buffer an already allocated buffer where to place the read value. Only
 * MAX_PATH bytes will be read out of socket.
 * @return 0 on success
 */
static int
pgagroal_management_read_conf_ls_detail(SSL* ssl, int socket, char* buffer)
{
   char buf[4];
   int size = 0;

   memset(&buf, 0, sizeof(buf));
   memset(buffer, 0, MAX_PATH);

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      goto error;
   }

   size = pgagroal_read_int32(&buf);

   if (size > MAX_PATH)
   {
      errno = EMSGSIZE;
      goto error;
   }

   if (read_complete(ssl, socket, buffer, size))
   {
      goto error;
   }

   return 0;

error:
   memset(buffer, 0, MAX_PATH);
   pgagroal_log_warn("pgagroal_management_read_conf_ls_detail: read: %d %s", socket, strerror(errno));
   errno = 0;

   return 1;
}

/**
 * Utility function to print out the result of a 'status'
 * or a 'status details' command already wrapped into a
 * JSON object.
 * The function tries to understand from the command name
 * within the JSON object if the output refers to the
 * 'status' or 'status details' command.
 *
 * If the command is faulty, this method does nothing, therefore
 * printing out information about faulty commands has to be done
 * at an higher level.
 *
 * @param json the JSON object
 *
 * @returns 0 on success
 */
int
pgagroal_management_json_print_status_details(cJSON* json)
{
   bool is_command_details = false; /* is this command 'status details' ? */
   int status = EXIT_STATUS_OK;
   bool previous_section_printed = false;  /* in 'status details', print an header bar between sections */

   // sanity check
   if (!json)
   {
      goto error;
   }

   // the command must be 'status' or 'status details'
   if (pgagroal_json_is_command_name_equals_to(json, "status"))
   {
      is_command_details = false;
   }
   else if (pgagroal_json_is_command_name_equals_to(json, "status details"))
   {
      is_command_details = true;
   }
   else
   {
      goto error;
   }

   // now get the output and start printing it
   cJSON* output = pgagroal_json_extract_command_output_object(json);

   // overall status
   printf("Status:              %s\n",
          cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(output, "status"), "message")->valuestring);

   // connections
   cJSON* connections = cJSON_GetObjectItemCaseSensitive(output, "connections");
   if (!connections)
   {
      goto error;
   }

   printf("Active connections:  %d\n", cJSON_GetObjectItemCaseSensitive(connections, "active")->valueint);
   printf("Total connections:   %d\n", cJSON_GetObjectItemCaseSensitive(connections, "total")->valueint);
   printf("Max connections:     %d\n", cJSON_GetObjectItemCaseSensitive(connections, "max")->valueint);

   // databases
   cJSON* databases = cJSON_GetObjectItemCaseSensitive(output, "databases");
   if (!databases)
   {
      goto error;
   }

   cJSON* disabled_databases = cJSON_GetObjectItemCaseSensitive(databases, "disabled");
   if (!disabled_databases)
   {
      goto error;
   }

   cJSON* disabled_databases_list = cJSON_GetObjectItemCaseSensitive(disabled_databases, JSON_TAG_ARRAY_NAME);
   cJSON* current;
   cJSON_ArrayForEach(current, disabled_databases_list)
   {
      printf("Disabled database:   %s\n", current->valuestring);
   }

   // the status command ends here
   if (!is_command_details)
   {
      goto end;
   }

   // dump the servers information
   cJSON* servers = cJSON_GetObjectItemCaseSensitive(output, "servers");
   if (!servers)
   {
      goto error;
   }

   cJSON* servers_list = cJSON_GetObjectItemCaseSensitive(servers, JSON_TAG_ARRAY_NAME);
   cJSON_ArrayForEach(current, servers_list)
   {
      if (!previous_section_printed)
      {
         printf("---------------------\n");
         previous_section_printed = true;
      }
      printf("Server:              %s\n", cJSON_GetObjectItemCaseSensitive(current, "server")->valuestring);
      printf("Host:                %s\n", cJSON_GetObjectItemCaseSensitive(current, "host")->valuestring);
      printf("Port:                %d\n", cJSON_GetObjectItemCaseSensitive(current, "port")->valueint);
      printf("State:               %s\n", cJSON_GetObjectItemCaseSensitive(current, "state")->valuestring);
      printf("---------------------\n");

   }

   // dump the limits information
   cJSON* limits = cJSON_GetObjectItemCaseSensitive(output, "limits");
   cJSON* limits_list = cJSON_GetObjectItemCaseSensitive(limits, JSON_TAG_ARRAY_NAME);
   cJSON_ArrayForEach(current, limits_list)
   {
      if (!previous_section_printed)
      {
         printf("---------------------\n");
         previous_section_printed = true;
      }
      printf("Database:            %s\n", cJSON_GetObjectItemCaseSensitive(current, "database")->valuestring);
      printf("Username:            %s\n", cJSON_GetObjectItemCaseSensitive(current, "username")->valuestring);
      cJSON* current_connections = cJSON_GetObjectItemCaseSensitive(current, "connections");
      printf("Active connections:  %d\n", cJSON_GetObjectItemCaseSensitive(current_connections, "active")->valueint);
      printf("Max connections:     %d\n", cJSON_GetObjectItemCaseSensitive(current_connections, "max")->valueint);
      printf("Initial connections: %d\n", cJSON_GetObjectItemCaseSensitive(current_connections, "initial")->valueint);
      printf("Min connections:     %d\n", cJSON_GetObjectItemCaseSensitive(current_connections, "min")->valueint);
      printf("---------------------\n");
   }

   // print the connection information
   cJSON_ArrayForEach(current, cJSON_GetObjectItemCaseSensitive(connections, JSON_TAG_ARRAY_NAME))
   {
      if (!previous_section_printed)
      {
         printf("---------------------\n");
         previous_section_printed = false;
      }

      printf("Connection %4d:     %-15s %-19s %-6s %-6s %s %s %s\n",
             cJSON_GetObjectItemCaseSensitive(current, "number")->valueint,
             cJSON_GetObjectItemCaseSensitive(current, "state")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "time")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "pid")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "fd")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "user")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "database")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "detail")->valuestring);

   }

   // all done
   goto end;

error:
   status = 1;
end:
   if (json)
   {
      cJSON_Delete(json);
   }

   return status;

}

/**
 * Utility method to get the information about the `conf ls` command.
 * This method produces a cJSON object that needs to be printed out in textual format.
 *
 * @param ssl the SSL file descriptor
 * @param socket the file descriptor for the socket
 *
 * @returns the cJSON object, faulty if something went wrong
 */
static cJSON*
pgagroal_management_json_read_conf_ls(SSL* ssl, int socket)
{
   char buf[4];
   char* buffer;
   char buf_info[MANAGEMENT_INFO_SIZE];
   int application;
   int version;

   if (read_complete(ssl, socket, &buf_info[0], sizeof(buf_info)))
   {
      pgagroal_log_warn("pgagroal_management_json_read_conf_ls: read: %d %s", socket, strerror(errno));
      errno = 0;
      return NULL;
   }

   application = pgagroal_read_int32(&(buf_info[2]));
   version = pgagroal_read_int32(&(buf_info[9]));

   char* version_buf = NULL;

   if (pgagroal_executable_version_string(&version_buf, version))
   {
      return NULL;
   }

   cJSON* json = pgagroal_json_create_new_command_object("conf ls", true, pgagroal_executable_name(application), version_buf);
   cJSON* output = pgagroal_json_extract_command_output_object(json);

   // add an array that will contain the files
   cJSON* files = cJSON_CreateObject();
   cJSON* files_array = cJSON_CreateArray();
   cJSON_AddItemToObject(output, "files", files);
   cJSON_AddItemToObject(files, JSON_TAG_ARRAY_NAME, files_array);

   memset(&buf, 0, sizeof(buf));
   buffer = calloc(1, MAX_PATH);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the main configuration file entry
   cJSON* mainConf = cJSON_CreateObject();
   cJSON_AddStringToObject(mainConf, "description", "Main Configuration file");
   cJSON_AddStringToObject(mainConf, "path", buffer);
   cJSON_AddItemToArray(files_array, mainConf);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the HBA file
   cJSON* hbaConf = cJSON_CreateObject();
   cJSON_AddStringToObject(hbaConf, "description", "HBA File");
   cJSON_AddStringToObject(hbaConf, "path", buffer);
   cJSON_AddItemToArray(files_array, hbaConf);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the limit file
   cJSON* limitConf = cJSON_CreateObject();
   cJSON_AddStringToObject(limitConf, "description", "Limit file");
   cJSON_AddStringToObject(limitConf, "path", buffer);
   cJSON_AddItemToArray(files_array, limitConf);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the frontend file
   cJSON* frontendConf = cJSON_CreateObject();
   cJSON_AddStringToObject(frontendConf, "description", "Frontend users file");
   cJSON_AddStringToObject(frontendConf, "path", buffer);
   cJSON_AddItemToArray(files_array, frontendConf);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the admins file
   cJSON* adminsConf = cJSON_CreateObject();
   cJSON_AddStringToObject(adminsConf, "description", "Admins file");
   cJSON_AddStringToObject(adminsConf, "path", buffer);
   cJSON_AddItemToArray(files_array, adminsConf);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the superuser file
   cJSON* superuserConf = cJSON_CreateObject();
   cJSON_AddStringToObject(superuserConf, "description", "Superuser file");
   cJSON_AddStringToObject(superuserConf, "path", buffer);
   cJSON_AddItemToArray(files_array, superuserConf);

   if (pgagroal_management_read_conf_ls_detail(ssl, socket, buffer))
   {
      goto error;
   }

   // add the users file
   cJSON* usersConf = cJSON_CreateObject();
   cJSON_AddStringToObject(usersConf, "description", "Users file");
   cJSON_AddStringToObject(usersConf, "path", buffer);
   cJSON_AddItemToArray(files_array, usersConf);

   // all done
   goto end;

error:
   free(buffer);
   pgagroal_log_warn("pgagroal_management_json_read_conf_ls: read: %d %s", socket, strerror(errno));
   errno = 0;
   pgagroal_json_set_command_object_faulty(json, strerror(errno), errno);

end:
   free(buffer);
   return json;

}

/**
 * Utility function to handle a JSON object and print it out
 * as normal text.
 *
 * @param json the JSON object
 * @returns 0 on success
 */
static int
pgagroal_management_json_print_conf_ls(cJSON* json)
{
   int status = EXIT_STATUS_OK;

   // sanity check
   if (!json)
   {
      goto error;
   }

   // now get the output and start printing it
   cJSON* output = pgagroal_json_extract_command_output_object(json);

   // files
   cJSON* files = cJSON_GetObjectItemCaseSensitive(output, "files");
   if (!files)
   {
      goto error;
   }

   cJSON* files_array = cJSON_GetObjectItemCaseSensitive(files, JSON_TAG_ARRAY_NAME);
   cJSON* current;
   cJSON_ArrayForEach(current, files_array)
   {
      // the current JSON object is made by two different values
      printf("%-25s : %s\n",
             cJSON_GetObjectItemCaseSensitive(current, "description")->valuestring,
             cJSON_GetObjectItemCaseSensitive(current, "path")->valuestring);
   }

   status = pgagroal_json_command_object_exit_status(json);
   goto end;

error:
   status = EXIT_STATUS_DATA_ERROR;
end:
   if (json)
   {
      cJSON_Delete(json);
   }

   return status;
}

static cJSON*
pgagroal_json_create_new_command_object(char* command_name, bool success, char* executable_name, char* executable_version)
{
   // root of the JSON structure
   cJSON* json = cJSON_CreateObject();

   if (!json)
   {
      goto error;
   }

   // the command structure
   cJSON* command = cJSON_CreateObject();
   if (!command)
   {
      goto error;
   }

   // insert meta-data about the command
   cJSON_AddStringToObject(command, JSON_TAG_COMMAND_NAME, command_name);
   cJSON_AddStringToObject(command, JSON_TAG_COMMAND_STATUS, success ? JSON_STRING_SUCCESS : JSON_STRING_ERROR);
   cJSON_AddNumberToObject(command, JSON_TAG_COMMAND_ERROR, success ? JSON_BOOL_SUCCESS : JSON_BOOL_ERROR);
   cJSON_AddNumberToObject(command, JSON_TAG_COMMAND_EXIT_STATUS, success ? 0 : EXIT_STATUS_DATA_ERROR);

   // the output of the command, this has to be filled by the caller
   cJSON* output = cJSON_CreateObject();
   if (!output)
   {
      goto error;
   }

   cJSON_AddItemToObject(command, JSON_TAG_COMMAND_OUTPUT, output);

   // who has launched the command ?
   cJSON* application = cJSON_CreateObject();
   if (!application)
   {
      goto error;
   }

   long minor = strtol(&executable_version[2], NULL, 10);
   if (errno == ERANGE || minor <= LONG_MIN || minor >= LONG_MAX)
   {
      goto error;
   }
   long patch = strtol(&executable_version[5], NULL, 10);
   if (errno == ERANGE || patch <= LONG_MIN || patch >= LONG_MAX)
   {
      goto error;
   }

   cJSON_AddStringToObject(application, JSON_TAG_APPLICATION_NAME, executable_name);
   cJSON_AddNumberToObject(application, JSON_TAG_APPLICATION_VERSION_MAJOR, executable_version[0] - '0');
   cJSON_AddNumberToObject(application, JSON_TAG_APPLICATION_VERSION_MINOR, (int)minor);
   cJSON_AddNumberToObject(application, JSON_TAG_APPLICATION_VERSION_PATCH, (int)patch);
   cJSON_AddStringToObject(application, JSON_TAG_APPLICATION_VERSION, executable_version);

   // add objects to the whole json thing
   cJSON_AddItemToObject(json, "command", command);
   cJSON_AddItemToObject(json, "application", application);

   return json;

error:
   if (json)
   {
      cJSON_Delete(json);
   }

   return NULL;

}

static cJSON*
pgagroal_json_extract_command_output_object(cJSON* json)
{
   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   return cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_OUTPUT);

error:
   return NULL;

}

static bool
pgagroal_json_is_command_name_equals_to(cJSON* json, char* command_name)
{
   if (!json || !command_name || strlen(command_name) <= 0)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* cName = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_NAME);
   if (!cName || !cJSON_IsString(cName) || !cName->valuestring)
   {
      goto error;
   }

   return !strncmp(command_name,
                   cName->valuestring,
                   MISC_LENGTH);

error:
   return false;
}

static int
pgagroal_json_set_command_object_faulty(cJSON* json, char* message, int exit_status)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* current = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_STATUS);
   if (!current)
   {
      goto error;
   }

   cJSON_SetValuestring(current, message);

   current = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_ERROR);
   if (!current)
   {
      goto error;
   }

   cJSON_SetIntValue(current, JSON_BOOL_ERROR);   // cannot use cJSON_SetBoolValue unless cJSON >= 1.7.16

   current = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_EXIT_STATUS);
   if (!current)
   {
      goto error;
   }

   cJSON_SetIntValue(current, exit_status);

   return 0;

error:
   return 1;

}

static int
pgagroal_json_command_object_exit_status(cJSON* json)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* status = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_EXIT_STATUS);
   if (!status || !cJSON_IsNumber(status))
   {
      goto error;
   }

   return status->valueint;

error:
   return EXIT_STATUS_DATA_ERROR;
}

static const char*
pgagroal_json_get_command_object_status(cJSON* json)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* status = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_STATUS);
   if (!cJSON_IsString(status) || (status->valuestring == NULL))
   {
      goto error;
   }

   return status->valuestring;
error:
   return NULL;

}

static int
pgagroal_json_print_and_free_json_object(cJSON* json)
{
   int status = pgagroal_json_command_object_exit_status(json);
   printf("%s\n", cJSON_Print(json));
   cJSON_Delete(json);
   return status;
}
