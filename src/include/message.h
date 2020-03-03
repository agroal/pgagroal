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

#ifndef PGAGROAL_MESSAGE_H
#define PGAGROAL_MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdbool.h>
#include <stdlib.h>

#define MESSAGE_STATUS_ZERO  0
#define MESSAGE_STATUS_OK    1
#define MESSAGE_STATUS_ERROR 2

/**
 * Read a message
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_message(int socket, struct message** msg);

/**
 * Read a message in blocking mode
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_block_message(int socket, struct message** msg);

/**
 * Read a message with a timeout
 * @param socket The socket descriptor
 * @param timeout The timeout in seconds
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_timeout_message(int socket, int timeout, struct message** msg);

/**
 * Write a message
 * @param socket The socket descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_write_message(int socket, struct message* msg);

/**
 * Write a message with NODELAY
 * @param socket The socket descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_write_nodelay_message(int socket, struct message* msg);

/**
 * Create a message
 * @param data A pointer to the data
 * @param length The length of the message
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_message(void* data, ssize_t length, struct message** msg);
   
/**
 * Copy a message
 * @param msg The resulting message
 * @return The copy
 */
struct message*
pgagroal_copy_message(struct message* msg);

/**
 * Free a message
 * @param msg The resulting message
 */
void
pgagroal_free_message(struct message* msg);

/**
 * Free a copy message
 * @param msg The resulting message
 */
void
pgagroal_free_copy_message(struct message* msg);

/**
 * Get the request identifier
 * @param msg The message
 * @return The identifier
 */
int32_t
pgagroal_get_request(struct message* msg);

/**
 * Extract the user name and database from a message
 * @param msg The message
 * @param username The resulting user name
 * @param database The resulting database
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_username_database(struct message* msg, char** username, char** database);

/**
 * Write an empty message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_empty(int socket);

/**
 * Write a notice message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_notice(int socket);

/**
 * Write a pool is full message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_pool_full(int socket);

/**
 * Write a connection refused message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_connection_refused(int socket);

/**
 * Write a bad password message
 * @param socket The socket descriptor
 * @param username The user name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_bad_password(int socket, char* username);

/**
 * Write an unsupported security model message
 * @param socket The socket descriptor
 * @param username The user name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_unsupported_security_model(int socket, char* username);

/**
 * Write a no HBA entry message
 * @param socket The socket descriptor
 * @param username The user name
 * @param database The database
 * @param address The client address
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_no_hba_entry(int socket, char* username, char* database, char* address);

/**
 * Write a deallocate all message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_deallocate_all(int socket);

/**
 * Write a reset all message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_reset_all(int socket);

/**
 * Write a terminate message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_terminate(int socket);

/**
 * Write an auth password message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_password(int socket);

/**
 * Create an auth password response message
 * @param password The password
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_password_response(char* password, struct message** msg);

/**
 * Write an auth md5 message
 * @param socket The socket descriptor
 * @param salt The salt
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_md5(int socket, char salt[4]);

/**
 * Create an auth MD5 response message
 * @param md5 The md5
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_md5_response(char* md5, struct message** msg);

/**
 * Write an auth SCRAM-SHA-256 message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_scram256(int socket);

/**
 * Create an auth SCRAM-SHA-256 response message
 * @param nounce The nounce
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_response(char* nounce, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue message
 * @param cn The client nounce
 * @param sn The server nounce
 * @param salt The salt
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_continue(char* cn, char* sn, char* salt, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue response message
 * @param wp The without proff
 * @param p The proff
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_continue_response(char* wp, char* p, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Final message
 * @param ss The server signature (BASE64)
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_final(char* ss, struct message** msg);

/**
 * Write an auth success message
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_success(int socket);

/**
 * Create a startup message
 * @param username The user name
 * @param database The database
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_startup_message(char* username, char* database, struct message** msg);

/**
 * Is the connection valid
 * @param socket The socket descriptor
 * @return true upon success, otherwise false
 */
bool
pgagroal_connection_isvalid(int socket);

#ifdef __cplusplus
}
#endif

#endif
