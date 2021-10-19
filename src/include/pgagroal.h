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
#include <openssl/ssl.h>

#define VERSION "1.3.3"

#define PGAGROAL_HOMEPAGE "https://agroal.github.io/pgagroal/"
#define PGAGROAL_ISSUES "https://github.com/agroal/pgagroal/issues"

#define MAIN_UDS ".s.pgagroal"

#define MAX_BUFFER_SIZE      65535
#define DEFAULT_BUFFER_SIZE  65535
#define SECURITY_BUFFER_SIZE   512

#define MAX_USERNAME_LENGTH   128
#define MAX_DATABASE_LENGTH   256
#define MAX_TYPE_LENGTH        16
#define MAX_ADDRESS_LENGTH     64
#define MAX_PASSWORD_LENGTH  1024
#define MAX_APPLICATION_NAME   64

#define MAX_PATH 1024
#define MISC_LENGTH 128
#define NUMBER_OF_SERVERS 64
#ifdef DEBUG
#define MAX_NUMBER_OF_CONNECTIONS 8
#else
#define MAX_NUMBER_OF_CONNECTIONS 10000
#endif
#define NUMBER_OF_HBAS     64
#define NUMBER_OF_LIMITS   64
#define NUMBER_OF_USERS    64
#define NUMBER_OF_ADMINS    8
#define NUMBER_OF_DISABLED 64

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

#define SECURITY_INVALID  -2
#define SECURITY_REJECT   -1
#define SECURITY_TRUST     0
#define SECURITY_PASSWORD  3
#define SECURITY_MD5       5
#define SECURITY_SCRAM256 10
#define SECURITY_ALL      99

#define AUTH_SUCCESS      0
#define AUTH_BAD_PASSWORD 1
#define AUTH_ERROR        2
#define AUTH_TIMEOUT      3

#define SERVER_NOTINIT         -2
#define SERVER_NOTINIT_PRIMARY -1
#define SERVER_PRIMARY          0
#define SERVER_REPLICA          1
#define SERVER_FAILOVER         2
#define SERVER_FAILED           3

#define FLUSH_IDLE       0
#define FLUSH_GRACEFULLY 1
#define FLUSH_ALL        2

#define VALIDATION_OFF        0
#define VALIDATION_FOREGROUND 1
#define VALIDATION_BACKGROUND 2

#define HISTOGRAM_BUCKETS 18

#define HUGEPAGE_OFF 0
#define HUGEPAGE_TRY 1
#define HUGEPAGE_ON  2

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

#define MAX(a, b)               \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a > _b ? _a : _b; })

#define MIN(a, b)               \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a < _b ? _a : _b; })

/**
 * The shared memory segment
 */
extern void* shmem;

/**
 * The shared memory segment for a pipeline
 */
extern void* pipeline_shmem;

/**
 * The shared memory segment for the Prometheus data
 */
extern void* prometheus_shmem;

/** @struct
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH]; /**< The name of the server */
   char host[MISC_LENGTH]; /**< The host name of the server */
   int port;               /**< The port of the server */
   atomic_schar state;     /**< The state of the server */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a connection
 */
struct connection
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char database[MAX_DATABASE_LENGTH]; /**< The database */
   char appname[MAX_APPLICATION_NAME]; /**< The application_name */

   bool new;           /**< Is the connection new */
   signed char server; /**< The server identifier */
   bool tx_mode;       /**< Connection in transaction mode */

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
   char type[MAX_TYPE_LENGTH];         /**< The type */
   char database[MAX_DATABASE_LENGTH]; /**< The database */
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char address[MAX_ADDRESS_LENGTH];   /**< The address / mask */
   char method[MAX_ADDRESS_LENGTH];    /**< The access method */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a limit entry
 */
struct limit
{
   char database[MAX_DATABASE_LENGTH]; /**< The database */
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   atomic_ushort active_connections;   /**< The active number of connections */
   int max_size;                       /**< The maximum pool size */
   int initial_size;                   /**< The initial pool size */
   int min_size;                       /**< The minimum pool size */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the Prometheus connection metric
 */
struct prometheus_connection
{
   atomic_ullong query_count;           /**< The number of queries per connection */
} __attribute__ ((aligned (64)));


/** @struct
 * Defines the Prometheus metrics
 */
struct prometheus
{
   atomic_ulong session_time[HISTOGRAM_BUCKETS]; /**< The histogram buckets */
   atomic_ulong session_time_sum;                /**< Total session time */

   atomic_ulong connection_error;       /**< The number of error calls */
   atomic_ulong connection_kill;        /**< The number of kill calls */
   atomic_ulong connection_remove;      /**< The number of remove calls */
   atomic_ulong connection_timeout;     /**< The number of timeout calls */
   atomic_ulong connection_return;      /**< The number of return calls */
   atomic_ulong connection_invalid;     /**< The number of invalid calls */
   atomic_ulong connection_get;         /**< The number of get calls */
   atomic_ulong connection_idletimeout; /**< The number of idle timeout calls */
   atomic_ulong connection_flush;       /**< The number of flush calls */
   atomic_ulong connection_success;     /**< The number of success calls */

   atomic_ulong auth_user_success;      /**< The number of AUTH_SUCCESS calls */
   atomic_ulong auth_user_bad_password; /**< The number of AUTH_BAD_PASSWORD calls */
   atomic_ulong auth_user_error;        /**< The number of AUTH_ERROR calls */

   atomic_ulong client_wait;            /**< The number of waiting clients */
   atomic_ulong client_active;          /**< The number of active clients */
   atomic_ulong client_wait_time;       /**< The time the client waits */

   atomic_ullong query_count;           /**< The number of queries */
   atomic_ullong tx_count;              /**< The number of transactions */

   atomic_ullong network_sent;          /**< The bytes sent by clients */
   atomic_ullong network_received;      /**< The bytes received from servers */

   atomic_int client_sockets;          /**< The number of sockets the client used */
   atomic_int self_sockets;            /**< The number of sockets used by pgagroal itself */

   atomic_ulong server_error[NUMBER_OF_SERVERS]; /**< The number of errors for a server */
   atomic_ulong failed_servers;                  /**< The number of failed servers */
   struct prometheus_connection prometheus_connections[];  /**< The number of prometheus connections (FMA) */

} __attribute__ ((aligned (64)));

/** @struct
 * Defines the configuration and state of pgagroal
 */
struct configuration
{
   char configuration_path[MAX_PATH]; /**< The configuration path */
   char hba_path[MAX_PATH];           /**< The HBA path */
   char limit_path[MAX_PATH];         /**< The limit path */
   char users_path[MAX_PATH];         /**< The users path */
   char frontend_users_path[MAX_PATH];/**< The frontend users path */
   char admins_path[MAX_PATH];        /**< The admins path */
   char superuser_path[MAX_PATH];     /**< The superuser path */

   char host[MISC_LENGTH]; /**< The host */
   int port;               /**< The port */
   int metrics;            /**< The metrics port */
   int management;         /**< The management port */
   bool gracefully;        /**< Is pgagroal in gracefully mode */

   char disabled[NUMBER_OF_DISABLED][MAX_DATABASE_LENGTH]; /**< Which databases are disabled */

   int pipeline; /**< The pipeline type */

   bool failover;                     /**< Is failover enabled */
   char failover_script[MISC_LENGTH]; /**< The failover script */

   int log_type;               /**< The logging type */
   int log_level;              /**< The logging level */
   char log_path[MISC_LENGTH]; /**< The logging path */
   bool log_connections;       /**< Log successful logins */
   bool log_disconnections;    /**< Log disconnects */
   atomic_schar log_lock;      /**< The logging lock */

   bool authquery; /**< Is authentication query enabled */

   bool tls;                        /**< Is TLS enabled */
   char tls_cert_file[MISC_LENGTH]; /**< TLS certificate path */
   char tls_key_file[MISC_LENGTH];  /**< TLS key path */
   char tls_ca_file[MISC_LENGTH];   /**< TLS CA certificate path */

   atomic_ushort active_connections; /**< The active number of connections */
   int max_connections;              /**< The maximum number of connections */
   bool allow_unknown_users;         /**< Allow unknown users */

   int blocking_timeout;         /**< The blocking timeout in seconds */
   int idle_timeout;             /**< The idle timeout in seconds */
   int validation;               /**< Validation mode */
   int background_interval;      /**< Background validation timer in seconds */
   int max_retries;              /**< The maximum number of retries */
   int authentication_timeout;   /**< The authentication timeout in seconds */
   int disconnect_client;        /**< Disconnect client if idle for more than the specified seconds */
   bool disconnect_client_force; /**< Force a disconnect client if active for more than the specified seconds */
   char pidfile[MISC_LENGTH];    /**< File containing the PID */

   char libev[MISC_LENGTH]; /**< Name of libev mode */
   int buffer_size;         /**< Socket buffer size */
   bool keep_alive;         /**< Use keep alive */
   bool nodelay;            /**< Use NODELAY */
   bool non_blocking;       /**< Use non blocking */
   int backlog;             /**< The backlog for listen */
   unsigned char hugepage;  /**< Huge page support */
   bool tracker;            /**< Tracker support */
   bool track_prepared_statements; /**< Track prepared statements (transaction pooling) */

   char unix_socket_dir[MISC_LENGTH]; /**< The directory for the Unix Domain Socket */

   atomic_schar su_connection; /**< The superuser connection */

   int number_of_servers;        /**< The number of servers */
   int number_of_hbas;           /**< The number of HBA entries */
   int number_of_limits;         /**< The number of limit entries */
   int number_of_users;          /**< The number of users */
   int number_of_frontend_users; /**< The number of users */
   int number_of_admins;         /**< The number of admins */

   atomic_schar states[MAX_NUMBER_OF_CONNECTIONS]; /**< The states */
   struct server servers[NUMBER_OF_SERVERS];       /**< The servers */
   struct hba hbas[NUMBER_OF_HBAS];                /**< The HBA entries */
   struct limit limits[NUMBER_OF_LIMITS];          /**< The limit entries */
   struct user users[NUMBER_OF_USERS];             /**< The users */
   struct user frontend_users[NUMBER_OF_USERS];    /**< The frontend users */
   struct user admins[NUMBER_OF_ADMINS];           /**< The admins */
   struct user superuser;                          /**< The superuser */
   struct connection connections[];                /**< The connections (FMA) */
} __attribute__ ((aligned (64)));

#ifdef __cplusplus
}
#endif

#endif
