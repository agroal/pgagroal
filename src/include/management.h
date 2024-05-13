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

#ifndef PGAGROAL_MANAGEMENT_H
#define PGAGROAL_MANAGEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

#define MANAGEMENT_TRANSFER_CONNECTION 1
#define MANAGEMENT_RETURN_CONNECTION   2
#define MANAGEMENT_KILL_CONNECTION     3
#define MANAGEMENT_FLUSH               4
#define MANAGEMENT_GRACEFULLY          5
#define MANAGEMENT_STOP                6
#define MANAGEMENT_STATUS              7
#define MANAGEMENT_DETAILS             8
#define MANAGEMENT_ISALIVE             9
#define MANAGEMENT_CANCEL_SHUTDOWN    10
#define MANAGEMENT_ENABLEDB           11
#define MANAGEMENT_DISABLEDB          12
#define MANAGEMENT_RESET              13
#define MANAGEMENT_RESET_SERVER       14
#define MANAGEMENT_CLIENT_DONE        15
#define MANAGEMENT_CLIENT_FD          16
#define MANAGEMENT_SWITCH_TO          17
#define MANAGEMENT_RELOAD             18
#define MANAGEMENT_REMOVE_FD          19
#define MANAGEMENT_CONFIG_GET         20
#define MANAGEMENT_CONFIG_SET         21
#define MANAGEMENT_CONFIG_LS          22
#define MANAGEMENT_GET_PASSWORD       23

/**
 * Status for the 'ping' (i.e., is-alive) command
 */
#define PING_STATUS_RUNNING 1
#define PING_STATUS_SHUTDOWN_GRACEFULLY 2

/**
 * Available command output formats
 */
#define COMMAND_OUTPUT_FORMAT_TEXT 'T'
#define COMMAND_OUTPUT_FORMAT_JSON 'J'

/**
 * Available applications
 */
#define PGAGROAL_EXECUTABLE 1
#define PGAGROAL_EXECUTABLE_CLI 2
#define PGAGROAL_EXECUTABLE_VAULT 3

/*
 * stores the application name and its version
 * which are sent through the socket
 */
struct pgagroal_version_info
{
   char s[2];
   int command;
   char v[3];
   int version;
};

/**
 * Get the frontend password of a user
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param user The frontend user
 * @param password The desired password
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_get_password(SSL* ssl, int socket, char* username, char* password);

/**
 * Write the frontend password of a user to the socket
 * @param socket The socket descriptor
 * @param password The frontend user
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_get_password(SSL* ssl, int socket, char* password);

/**
 * Read the management header
 * @param socket The socket descriptor
 * @param id The resulting management identifier
 * @param slot The resulting slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_header(int socket, signed char* id, int32_t* slot);

/**
 * Read the management payload
 * @param socket The socket descriptor
 * @param id The management identifier
 * @param payload_i The resulting integer payload
 * @param payload_s The resulting string payload
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_payload(int socket, signed char id, int* payload_i, char** payload_s);

/**
 * Management operation: Transfer a connection
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_transfer_connection(int32_t slot);

/**
 * Management operation: Return a connection
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_return_connection(int32_t slot);

/**
 * Management operation: Kill a connection
 * @param slot The slot
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_kill_connection(int32_t slot, int socket);

/**
 * Management operation: Flush the pool
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param mode The flush mode
 * @param database The database
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_flush(SSL* ssl, int socket, int32_t mode, char* database);

/**
 * Management operation: Enable database
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param database The database name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_enabledb(SSL* ssl, int socket, char* database);

/**
 * Management operation: Disable database
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param database The database name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_disabledb(SSL* ssl, int socket, char* database);

/**
 * Management operation: Gracefully
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_gracefully(SSL* ssl, int socket);

/**
 * Management operation: Stop
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_stop(SSL* ssl, int socket);

/**
 * Management operation: Cancel shutdown
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_cancel_shutdown(SSL* ssl, int socket);

/**
 * Management operation: Status
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_status(SSL* ssl, int socket);

/**
 * Management: Read status
 * @param socket The socket
 * @param output_format a char describing the type of output (text or json)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_status(SSL* ssl, int socket, char output_format);

/**
 * Management: Write status
 * @param socket The socket
 * @param graceful Is pgagroal in graceful shutdown
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_status(int socket, bool graceful);

/**
 * Management operation: Details
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_details(SSL* ssl, int socket);

/**
 * Management: Read details
 * @param socket The socket
 * @param output_format the output format for this command (text, json)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_details(SSL* ssl, int socket, char output_format);

/**
 * Management: Write details
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_details(int socket);

/**
 * Management operation: isalive
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_isalive(SSL* ssl, int socket);

/**
 * Management: Read isalive
 * @param socket The socket
 * @param status The resulting status
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_isalive(SSL* ssl, int socket, int* status, char output_format);

/**
 * Management: Write isalive
 * @param socket The socket
 * @param gracefully Is the server shutting down gracefully
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_isalive(int socket, bool gracefully);

/**
 * Management operation: Reset
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_reset(SSL* ssl, int socket);

/**
 * Management operation: Reset server
 * @param ssl The SSL connection
 * @param socket The socket
 * @param server The server
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_reset_server(SSL* ssl, int socket, char* server);

/**
 * Management operation: Client done
 * @param pid The pid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_client_done(pid_t pid);

/**
 * Management operation: Client file descriptor
 * @param slot The slot
 * @param pid The pid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_client_fd(int32_t slot, pid_t pid);

/**
 * Management operation: Switch to
 * @param ssl The SSL connection
 * @param socket The socket
 * @param server The server
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_switch_to(SSL* ssl, int socket, char* server);

/**
 * Management operation: Reload
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_reload(SSL* ssl, int socket);

/**
 * Management operation: Remove socket descriptor
 * @param slot The slot
 * @param socket The socket
 * @param pid The pid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_remove_fd(int32_t slot, int socket, pid_t pid);

/**
 * Management operation: get a configuration setting.
 * This function sends over the socket the message to get a configuration
 * value.
 * In particular, the message block for the action config_get is sent,
 * then the size of the configuration parameter to get (e.g., `max_connections`)
 * and last the parameter name itself.
 *
 * @param ssl the SSL connection
 * @param socket the socket file descriptor
 * @param config_key the name of the configuration parameter to get back
 * @return 0 on success, 1 on error
 */
int
pgagroal_management_config_get(SSL* ssl, int socket, char* config_key);

/**
 * Management operation result: receives the key to read in the configuration.
 *
 * Internally, exploits the function to read the payload from the socket.
 * @see pgagroal_management_read_payload
 *
 * @param ssl the socket file descriptor
 * @param config_key the key to read (is used only to print in the output)
 * @param verbose verbosity flag
 * @param output_format the output format
 * @param expected_value if set, a value that the configuration should match
 * @return 0 on success
 */
int
pgagroal_management_read_config_get(int socket, char* config_key, char* expected_value, bool verbose, char output_format);

/**
 * Management operation: write the result of a config_get action on the socket.
 *
 * Writes on the socket the result of the request for a specific
 * configuration parameter.
 *
   ° @param socket the socket file descriptor
 * @param config_key the name of the configuration parameter to get
 * @return 0 on success
 */
int
pgagroal_management_write_config_get(int socket, char* config_key);

/**
 * Management operation: set a configuration setting.
 * This function sends over the socket the message to set a configuration
 * value.
 * In particular, the message block for the action config_set is sent,
 * then the size of the configuration parameter to set (e.g., `max_connections`),
 * then the parameter name. At this point another couple of "size" and "value" is
 * sent with the size of the value to set and its value.
 *
 * @param ssl the SSL connection
 * @param socket the socket file descriptor
 * @param config_key the name of the configuration parameter to set
 * @param config_value the value to set for the new parameter
 * @return 0 on success, 1 on error
 */
int
pgagroal_management_config_set(SSL* ssl, int socket, char* config_key, char* config_value);

/**
 * Function to execute the config-set and write over the socket
 * the result.
 *
 * If the parameter is set, the function calls the
 * pgagroal_management_write_config_get to send back over the
 * socket the current value of the parameter. Therefore,
 * this function answers always back the current value
 * so that it is possible to reason about the new value and
 * see if it has changed.
 *
 * @param socket the socket to use for communication
 * @param config_key the key to set
 * @param config_value the value to use
 * @return 0 on success
 */
int
pgagroal_management_write_config_set(int socket, char* config_key, char* config_value);

/**
 * Entry point for managing the `conf ls` command that
 * will list all the configuration files used by the running
 * daemon.
 *
 * @param ssl the SSL handler
 * @param fd the socket file descriptor
 * @returns 0 on success
 */
int
pgagroal_management_conf_ls(SSL* ssl, int fd);

/**
 * Reads out of the socket the list of configuration
 * files and prints them out to the standard output.
 *
 * The order of the read paths is:
 * - configuration path
 * - HBA path
 * - limit path
 * - frontend users path
 * - admins path
 * - Superusers path
 * - users path
 *
 * @param socket the file descriptor of the open socket
 * @param ssl the SSL handler
 * @param output_format the format to output the command result
 * @returns 0 on success
 */
int
pgagroal_management_read_conf_ls(SSL* ssl, int socket, char output_format);

/**
 * The management function responsible for sending
 * the configuration paths into the socket.
 *
 * The function sends every path following the path length,
 * that must be limited to MAX_PATH size.
 *
 * The order of the sent paths is:
 * - configuration path
 * - HBA path
 * - limit path
 * - frontend users path
 * - admins path
 * - Superusers path
 * - users path
 *
 * @params socket the file descriptor of the open socket
 * @returns 0 on success
 */
int
pgagroal_management_write_conf_ls(int socket);

#ifdef __cplusplus
}
#endif

#endif
