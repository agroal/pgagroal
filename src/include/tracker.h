/*
 * Copyright (C) 2023 Red Hat
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

#ifndef PGAGROAL_TRACKER_H
#define PGAGROAL_TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdlib.h>

#define TRACKER_CLIENT_START                0
#define TRACKER_CLIENT_STOP                 1

#define TRACKER_GET_CONNECTION_SUCCESS      2
#define TRACKER_GET_CONNECTION_TIMEOUT      3
#define TRACKER_GET_CONNECTION_ERROR        4
#define TRACKER_RETURN_CONNECTION_SUCCESS   5
#define TRACKER_RETURN_CONNECTION_KILL      6
#define TRACKER_KILL_CONNECTION             7

#define TRACKER_AUTHENTICATE                8

#define TRACKER_BAD_CONNECTION              9
#define TRACKER_IDLE_TIMEOUT               10
#define TRACKER_MAX_CONNECTION_AGE         11
#define TRACKER_INVALID_CONNECTION         12
#define TRACKER_FLUSH                      13
#define TRACKER_REMOVE_CONNECTION          14

#define TRACKER_PREFILL                    15
#define TRACKER_PREFILL_RETURN             16
#define TRACKER_PREFILL_KILL               17
#define TRACKER_WORKER_RETURN1             18
#define TRACKER_WORKER_RETURN2             19
#define TRACKER_WORKER_KILL1               20
#define TRACKER_WORKER_KILL2               21

#define TRACKER_TX_RETURN_CONNECTION_START 30
#define TRACKER_TX_RETURN_CONNECTION_STOP  31
#define TRACKER_TX_GET_CONNECTION          32
#define TRACKER_TX_RETURN_CONNECTION       33

#define TRACKER_SOCKET_ASSOCIATE_CLIENT    100
#define TRACKER_SOCKET_ASSOCIATE_SERVER    101
#define TRACKER_SOCKET_DISASSOCIATE_CLIENT 102
#define TRACKER_SOCKET_DISASSOCIATE_SERVER 103

/**
 * Tracking event: Basic
 * @param id The event identifier
 * @param username The user name
 * @param database The database
 */
void
pgagroal_tracking_event_basic(int id, char* username, char* database);

/**
 * Tracking event: Slot
 * @param id The event identifier
 * @param slot The slot
 */
void
pgagroal_tracking_event_slot(int id, int slot);

/**
 * Tracking event: Socket
 * @param id The event identifier
 * @param socket The socket
 */
void
pgagroal_tracking_event_socket(int id, int socket);

#ifdef __cplusplus
}
#endif

#endif
