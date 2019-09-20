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

#ifndef PGAGROAL_MANAGEMENT_H
#define PGAGROAL_MANAGEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdlib.h>

#define MANAGEMENT_TRANSFER_CONNECTION 1
#define MANAGEMENT_RETURN_CONNECTION   2
#define MANAGEMENT_KILL_CONNECTION     3
#define MANAGEMENT_FLUSH               4
#define MANAGEMENT_GRACEFULLY          5
#define MANAGEMENT_STOP                6

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
 * @param payload The resulting payload
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_payload(int socket, signed char id, int* payload);

/**
 * Free the management payload
 * @param payload The payload
 */
void
pgagroal_management_free_payload(void* payload);

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
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_kill_connection(void* shmem, int32_t slot);

/**
 * Management operation: Flush the pool
 * @param shmem The shared memory segment
 * @param mode The flush mode
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_flush(void* shmem, int32_t mode);

/**
 * Management operation: Gracefully
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_gracefully(void* shmem);

/**
 * Management operation: Stop
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_stop(void* shmem);

#ifdef __cplusplus
}
#endif

#endif
