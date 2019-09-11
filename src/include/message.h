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
 *
 */
int
pgagroal_read_message(int socket, struct message** msg);

/**
 *
 */
int
pgagroal_read_block_message(int socket, struct message** msg);

/**
 *
 */
int
pgagroal_write_message(int socket, struct message* msg);

/**
 *
 */
int
pgagroal_write_nodelay_message(int socket, struct message* msg);

/**
 *
 */
int
pgagroal_create_message(void* data, ssize_t length, struct message** msg);
   
/**
 *
 */
struct message*
pgagroal_copy_message(struct message* msg);

/**
 *
 */
void
pgagroal_free_message(struct message* msg);

/**
 *
 */
void
pgagroal_free_copy_message(struct message* msg);

/**
 *
 */
int32_t
pgagroal_get_request(struct message* msg);

/**
 *
 */
int
pgagroal_extract_username_database(struct message* msg, char** username, char** database);

/**
 *
 */
int
pgagroal_write_empty(int socket);

/**
 *
 */
int
pgagroal_write_notice(int socket);

/**
 *
 */
int
pgagroal_write_pool_full(int socket);

/**
 *
 */
int
pgagroal_write_bad_password(int socket, char* username);

/**
 *
 */
int
pgagroal_write_unsupported_security_model(int socket, char* username);

/**
 *
 */
int
pgagroal_write_no_hba_entry(int socket, char* username, char* database, char* address);

/**
 *
 */
int
pgagroal_write_deallocate_all(int socket);

/**
 *
 */
int
pgagroal_write_terminate(int socket);

/**
 *
 */
bool
pgagroal_connection_isvalid(int socket);

#ifdef __cplusplus
}
#endif

#endif
