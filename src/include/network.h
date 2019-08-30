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

#ifndef PGAGROAL_NETWORK_H
#define PGAGROAL_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <sys/socket.h>

/**
 *
 */
int
pgagroal_bind(const char* hostname, int port, void* shmem, int** fds, int* length);

/**
 *
 */
int
pgagroal_bind_unix_socket(const char* directory);

/**
 *
 */
int
pgagroal_connect(void* shmem, const char* hostname, int port, int* fd);

/**
 *
 */
int
pgagroal_connect_unix_socket(const char* directory, int* fd);

/**
 *
 */
int
pgagroal_disconnect(int fd);

/**
 *
 */
void*
pgagroal_get_sockaddr(struct sockaddr *sa);
   
/**
 *
 */
int
pgagroal_is_ready(int fd);
   
/**
 *
 */
int
pgagroal_tcp_nodelay(int fd, void* shmem);

/**
 *
 */
int
pgagroal_socket_buffers(int fd, void* shmem);

/**
 *
 */
int
pgagroal_socket_nonblocking(int fd, void* shmem);

#ifdef __cplusplus
}
#endif

#endif
