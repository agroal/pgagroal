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

#ifndef PGAGROAL_NETWORK_H
#define PGAGROAL_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <sys/socket.h>

/**
 * Bind sockets for a host
 * @param hostname The host name
 * @param port The port number
 * @param shmem The shared memory segment
 * @param fds The resulting descriptors
 * @param length The resulting length of descriptors
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_bind(const char* hostname, int port, void* shmem, int** fds, int* length);

/**
 * Bind a Unix Domain Socket
 * @param directory The directory
 * @param shmem The shared memory segment
 * @param fd The resulting descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_bind_unix_socket(const char* directory, void* shmem, int* fd);

/**
 * Connect to a host
 * @param shmem The shared memory segment
 * @param hostname The host name
 * @param port The port number
 * @param fd The resulting descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connect(void* shmem, const char* hostname, int port, int* fd);

/**
 * Connect to a Unix Domain Socket
 * @param directory The directory
 * @param fd The resulting descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connect_unix_socket(const char* directory, int* fd);

/**
 * Is the socket valid
 * @param fd The descriptor
 * @return True upon success, otherwise false
 */
bool
pgagroal_socket_isvalid(int fd);

/**
 * Shutdown a descriptor
 * @param fd The descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_shutdown(int fd);

/**
 * Disconnect from a descriptor
 * @param fd The descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_disconnect(int fd);

/**
 * Get the sockaddr_in structure
 * @param sa The sockaddr structure
 * @return The sockaddr_in / sockaddr_in6 structure
 */
void*
pgagroal_get_sockaddr(struct sockaddr *sa);
   
/**
 * Get the address of a sockaddr
 * @param sa The sockaddr structure
 * @param address The result address
 * @param length The length
 */
void
pgagroal_get_address(struct sockaddr *sa, char* address, size_t length);

/**
 * Apply TCP/NODELAY to a descriptor
 * @param fd The descriptor
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tcp_nodelay(int fd, void* shmem);

/**
 * Set the configured socket buffer size to a descriptor
 * @param fd The descriptor
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_socket_buffers(int fd, void* shmem);

/**
 * Apply O_NONBLOCK to a descriptor
 * @param fd The descriptor
 * @param value The value
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_socket_nonblocking(int fd, bool value);

#ifdef __cplusplus
}
#endif

#endif
