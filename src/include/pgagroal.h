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

#include <ev.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>

#define VERSION "0.4.0"

/* Setup zf_log to include DEBUG support even for release builds */
#ifdef DEBUG
#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#else
#define ZF_LOG_LEVEL ZF_LOG_DEBUG
#endif

#define MAX_BUFFER_SIZE      65535
#define DEFAULT_BUFFER_SIZE  65535
#define SECURITY_BUFFER_SIZE   512

#define IDENTIFIER_LENGTH 64
#define MISC_LENGTH 128
#define NUMBER_OF_SERVERS 64
#ifdef DEBUG
#define MAX_NUMBER_OF_CONNECTIONS 8
#else
#define MAX_NUMBER_OF_CONNECTIONS 1000
#endif
#define NUMBER_OF_HBAS   64
#define NUMBER_OF_LIMITS 64
#define NUMBER_OF_USERS  64

#define NUMBER_OF_SECURITY_MESSAGES 5

#define STATE_NOTINIT    -2
#define STATE_INIT       -1
#define STATE_FREE        0
#define STATE_IN_USE      1
#define STATE_GRACEFULLY  2
#define STATE_FLUSH       3
#define STATE_IDLE_CHECK  4
#define STATE_VALIDATION  5
#define STATE_REMOVE      6

#define AUTH_SUCCESS 0
#define AUTH_FAILURE 1

#define SERVER_NOTINIT         -2
#define SERVER_NOTINIT_PRIMARY -1
#define SERVER_PRIMARY          0
#define SERVER_REPLICA          1

#define FLUSH_IDLE       0
#define FLUSH_GRACEFULLY 1
#define FLUSH_ALL        2

#define VALIDATION_OFF        0
#define VALIDATION_FOREGROUND 1
#define VALIDATION_BACKGROUND 2

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

/** @struct
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH]; /**< The name of the server */
   char host[MISC_LENGTH]; /**< The host name of the server */
   int port;               /**< The port of the server */
   int primary;            /**< The status of the server */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a connection
 */
struct connection
{
   char username[IDENTIFIER_LENGTH]; /**< The user name */
   char database[IDENTIFIER_LENGTH]; /**< The database */

   bool new;             /**< Is the connection new */
   unsigned char server; /**< The server identifier */

   signed char has_security;                                                  /**< The security identifier */
   ssize_t security_lengths[NUMBER_OF_SECURITY_MESSAGES];                     /**< The lengths of the security messages */
   char security_messages[NUMBER_OF_SECURITY_MESSAGES][SECURITY_BUFFER_SIZE]; /**< The security messages */

   signed char limit_rule; /**< The limit rule used */
   time_t timestamp;       /**< The last used timestamp */
   pid_t pid;              /**< The associated process id */
   int fd;                 /**< The descriptor */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a HBA entry
 */
struct hba
{
   char type[16];                    /**< The type */
   char database[IDENTIFIER_LENGTH]; /**< The database */
   char username[IDENTIFIER_LENGTH]; /**< The user name */
   char address[IDENTIFIER_LENGTH];  /**< The address / mask */
   char method[IDENTIFIER_LENGTH];   /**< The access method */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a limit entry
 */
struct limit
{
   char database[IDENTIFIER_LENGTH]; /**< The database */
   char username[IDENTIFIER_LENGTH]; /**< The user name */
   atomic_ushort active_connections; /**< The active number of connections */
   int max_connections;              /**< The maximum number of connections */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a user
 */
struct user
{
   char username[IDENTIFIER_LENGTH]; /**< The user name */
   char password[IDENTIFIER_LENGTH]; /**< The password */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the configuration and state of pgagroal
 */
struct configuration
{
   char host[MISC_LENGTH]; /**< The host */
   int port;               /**< The port */

   int log_type;               /**< The logging type */
   int log_level;              /**< The logging level */
   char log_path[MISC_LENGTH]; /**< The logging path */

   atomic_ushort active_connections; /**< The active number of connections */
   int max_connections;              /**< The maximum number of connections */

   int blocking_timeout;    /**< The blocking timeout in seconds */
   int idle_timeout;        /**< The idle timeout in seconds */
   int validation;          /**< Validation mode */
   int background_interval; /**< Background validation timer in seconds */
   int max_retries;         /**< The maximum number of retries */

   char libev[MISC_LENGTH]; /**< Name of libev mode */
   int buffer_size;         /**< Socket buffer size */
   bool keep_alive;         /**< Use keep alive */
   bool nodelay;            /**< Use NODELAY */
   bool non_blocking;       /**< Use non blocking */
   int backlog;             /**< The backlog for listen */

   char unix_socket_dir[MISC_LENGTH]; /**< The directory for the Unix Domain Socket */

   int number_of_servers; /**< The number of servers */
   int number_of_hbas;    /**< The number of HBA entries */
   int number_of_limits;  /**< The number of limit entries */
   int number_of_users;   /**< The number of users */

   atomic_schar states[MAX_NUMBER_OF_CONNECTIONS]; /**< The states */
   struct server servers[NUMBER_OF_SERVERS];       /**< The servers */
   struct hba hbas[NUMBER_OF_HBAS];                /**< The HBA entries */
   struct limit limits[NUMBER_OF_LIMITS];          /**< The limit entries */
   struct user users[NUMBER_OF_USERS];             /**< The users */
   struct connection connections[];                /**< The connections (FMA) */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a message
 */
struct message
{
   signed char kind;  /**< The kind of the message */
   ssize_t length;    /**< The length of the message */
   size_t max_length; /**< The maximum size of the message */
   void* data;        /**< The message data */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   void* shmem;             /**< The shared memory segment */
   int slot;                /**< The slot */
};

#ifdef __cplusplus
}
#endif

#endif
