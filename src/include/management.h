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
 * @param shmem The shared memory segment
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_transfer_connection(void* shmem, int32_t slot);

/**
 * Management operation: Return a connection
 * @param shmem The shared memory segment
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_return_connection(void* shmem, int32_t slot);

/**
 * Management operation: Kill a connection
 * @param shmem The shared memory segment
 * @param slot The slot
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_kill_connection(void* shmem, int32_t slot, int socket);

/**
 * Management operation: Flush the pool
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param mode The flush mode
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_flush(SSL* ssl, int socket, int32_t mode);

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
 * @param shmem The shared memory segment
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
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_status(SSL* ssl, int socket);

/**
 * Management: Write status
 * @param socket The socket
 * @param graceful Is pgagroal in graceful shutdown
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_status(int socket, bool graceful, void* shmem);

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
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_details(SSL* ssl, int socket);

/**
 * Management: Write details
 * @param socket The socket
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_details(int socket, void* shmem);

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
pgagroal_management_read_isalive(SSL* ssl, int socket, int* status);

/**
 * Management: Write isalive
 * @param socket The socket
 * @param gracefully Is the server shutting down gracefully
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_isalive(int socket, bool gracefully, void* shmem);

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
 * @param shmem The shared memory segment
 * @param pid The pid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_client_done(void* shmem, pid_t pid);

/**
 * Management operation: Client file descriptor
 * @param shmem The shared memory segment
 * @param slot The slot
 * @param pid The pid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_client_fd(void* shmem, int32_t slot, pid_t pid);

/**
 * Management operation: Switch to
 * @param ssl The SSL connection
 * @param socket The socket
 * @param server The server
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_switch_to(SSL* ssl, int socket, char* server);

#ifdef __cplusplus
}
#endif

#endif
