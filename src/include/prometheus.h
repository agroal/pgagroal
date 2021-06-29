/*
 * Copyright (C) 2021 Red Hat
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

#ifndef PGAGROAL_PROMETHEUS_H
#define PGAGROAL_PROMETHEUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
#include <stdlib.h>

/**
 * Create a prometheus instance
 * @param fd The client descriptor
 */
void
pgagroal_prometheus(int fd);

/**
 * Initialize prometheus shmem
 */
int
pgagroal_init_prometheus(size_t* p_size, void** p_shmem);

/**
 * Add session time information
 * @param time The time
 */
void
pgagroal_prometheus_session_time(double time);

/**
 * Connection error
 */
void
pgagroal_prometheus_connection_error(void);

/**
 * Connection kill
 */
void
pgagroal_prometheus_connection_kill(void);

/**
 * Connection remove
 */
void
pgagroal_prometheus_connection_remove(void);

/**
 * Connection timeout
 */
void
pgagroal_prometheus_connection_timeout(void);

/**
 * Connection return
 */
void
pgagroal_prometheus_connection_return(void);

/**
 * Connection invalid
 */
void
pgagroal_prometheus_connection_invalid(void);

/**
 * Connection get
 */
void
pgagroal_prometheus_connection_get(void);

/**
 * Connection idle timeout
 */
void
pgagroal_prometheus_connection_idletimeout(void);

/**
 * Connection flush
 */
void
pgagroal_prometheus_connection_flush(void);

/**
 * Connection success
 */
void
pgagroal_prometheus_connection_success(void);

/**
 * Increase AUTH_SUCCESS for a user
 */
void
pgagroal_prometheus_auth_user_success(void);

/**
 * Increase AUTH_BAD_PASSWORD for a user
 */
void
pgagroal_prometheus_auth_user_bad_password(void);

/**
 * Increase AUTH_ERROR for a user
 */
void
pgagroal_prometheus_auth_user_error(void);

/**
 * Reset the counters and histograms
 */
void
pgagroal_prometheus_reset(void);

/**
 * Increase SERVER_ERROR for a server
 * @param server The server
 */
void
pgagroal_prometheus_server_error(int server);

/**
 * Count failed servers
 */
void
pgagroal_prometheus_failed_servers(void);

#ifdef __cplusplus
}
#endif

#endif

