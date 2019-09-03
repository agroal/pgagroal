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

#ifndef PGAGROAL_H
#define PGAGROAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>

/* Setup zf_log to include DEBUG support even for release builds */
#ifdef DEBUG
#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#else
#define ZF_LOG_LEVEL ZF_LOG_DEBUG
#endif

#define MAX_BUFFER_SIZE      65535
#define DEFAULT_BUFFER_SIZE  65535
#define SECURITY_BUFFER_SIZE  2048

#define IDENTIFIER_LENGTH 64
#define MISC_LENGTH 128
#define NUMBER_OF_SERVERS 64
#ifdef DEBUG
#define NUMBER_OF_CONNECTIONS 8
#else
#define NUMBER_OF_CONNECTIONS 512
#endif
#define NUMBER_OF_HBAS 64

#define NUMBER_OF_SECURITY_MESSAGES 5

#define DEFAULT_BACKLOG NUMBER_OF_CONNECTIONS / 4

#define STATE_NOTINIT    -2
#define STATE_INIT       -1
#define STATE_FREE        0
#define STATE_IN_USE      1
#define STATE_GRACEFULLY  2
#define STATE_FLUSH       3
#define STATE_IDLE_CHECK  4

#define AUTH_SUCCESS 0
#define AUTH_FAILURE 1

#define SERVER_NOTINIT         -2
#define SERVER_NOTINIT_PRIMARY -1
#define SERVER_PRIMARY          0
#define SERVER_REPLICA          1

#define FLUSH_IDLE       0
#define FLUSH_GRACEFULLY 1
#define FLUSH_ALL        2

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

struct server
{
   char name[MISC_LENGTH];
   char host[MISC_LENGTH];
   int port;
   int primary;
} __attribute__ ((aligned (64)));

struct connection
{
   atomic_int state;
   bool new;
   int server;
   char username[IDENTIFIER_LENGTH];
   char database[IDENTIFIER_LENGTH];
   int has_security;
   ssize_t security_lengths[NUMBER_OF_SECURITY_MESSAGES];
   char security_messages[NUMBER_OF_SECURITY_MESSAGES][SECURITY_BUFFER_SIZE];
   time_t timestamp;
   int fd;
} __attribute__ ((aligned (64)));

struct hba
{
   char type[6];
   char database[IDENTIFIER_LENGTH];
   char username[IDENTIFIER_LENGTH];
   char cidr[IDENTIFIER_LENGTH];
   char authentication[IDENTIFIER_LENGTH];
} __attribute__ ((aligned (64)));

struct configuration
{
   char host[MISC_LENGTH];
   int port;

   int log_type;
   int log_level;
   char log_path[MISC_LENGTH];

   atomic_int number_of_connections;
   int max_connections;

   int idle_timeout;

   int buffer_size;
   bool keep_alive;
   bool nodelay;
   bool non_blocking;
   int backlog;

   char unix_socket_dir[MISC_LENGTH];
   
   struct server servers[NUMBER_OF_SERVERS];
   struct connection connections[NUMBER_OF_CONNECTIONS];
   struct hba hbas[NUMBER_OF_HBAS];
} __attribute__ ((aligned (64)));

struct message
{
   signed char kind;
   ssize_t length;
   size_t max_length;
   void* data;
} __attribute__ ((aligned (64)));

#ifdef __cplusplus
}
#endif

#endif
