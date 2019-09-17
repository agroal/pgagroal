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

#ifndef PGAGROAL_POOL_H
#define PGAGROAL_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdlib.h>

/**
 * Get a connection
 * @param shmem The shared memory segment
 * @param username The user name
 * @param database The database
 * @param slot The resulting slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_get_connection(void* shmem, char* username, char* database, int* slot);

/**
 * Return a connection
 * @param shmem The shared memory segment
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_return_connection(void* shmem, int slot);

/**
 * Kill a connection
 * @param shmem The shared memory segment
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_kill_connection(void* shmem, int slot);

/**
 * Perform idle timeout
 * @param shmem The shared memory segment
 */
void
pgagroal_idle_timeout(void* shmem);

/**
 * Perform connection validation
 * @param shmem The shared memory segment
 */
void
pgagroal_validation(void* shmem);

/**
 * Flush the pool
 * @param shmem The shared memory segment
 * @param mode The flush mode
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_flush(void* shmem, int mode);

/**
 * Initialize the pool
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_init(void* shmem);

/**
 * Shutdown the pool
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_shutdown(void* shmem);

/**
 * Print the status of the pool
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_status(void* shmem);

#ifdef __cplusplus
}
#endif

#endif
