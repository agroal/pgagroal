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

#ifndef PGAGROAL_POOL_H
#define PGAGROAL_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdbool.h>
#include <stdlib.h>

/**
 * Get a connection
 * @param username The user name
 * @param database The database
 * @param reuse Should a slot be reused
 * @param transaction_mode Obtain a connection in transaction mode
 * @param slot The resulting slot
 * @return 0 upon success, 1 if pool is full, otherwise 2
 */
int
pgagroal_get_connection(char* username, char* database, bool reuse, bool transaction_mode, int* slot);

/**
 * Return a connection
 * @param slot The slot
 * @param transaction_mode Is the connection returned in transaction mode
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_return_connection(int slot, bool transaction_mode);

/**
 * Kill a connection
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_kill_connection(int slot);

/**
 * Perform idle timeout
 */
void
pgagroal_idle_timeout();

/**
 * Perform connection validation
 */
void
pgagroal_validation();

/**
 * Flush the pool
 * @param mode The flush mode
 */
void
pgagroal_flush(int mode);

/**
 * Prefill the pool
 * @param initial Use initial size
 */
void
pgagroal_prefill(bool initial);

/**
 * Initialize the pool
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_init();

/**
 * Shutdown the pool
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_shutdown();

/**
 * Print the status of the pool
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_status();

#ifdef __cplusplus
}
#endif

#endif
