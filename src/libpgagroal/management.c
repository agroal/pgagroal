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

/* pgagroal */
#include <pgagroal.h>
#include <logging.h>
#include <network.h>
#include <management.h>
#include <message.h>
#include <pool.h>
#include <utils.h>

#define ZF_LOG_TAG "management"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MANAGEMENT_HEADER_SIZE 5

static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);
static int write_header(SSL* ssl, int fd, signed char type, int slot);

int
pgagroal_management_read_header(int socket, signed char* id, int32_t* slot)
{
   char header[MANAGEMENT_HEADER_SIZE];

   if (read_complete(NULL, socket, &header[0], sizeof(header)))
   {
      ZF_LOGW("pgagroal_management_read_header: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *id = pgagroal_read_byte(&(header));
   *slot = pgagroal_read_int32(&(header[1]));
   
   return 0;

error:

   *id = -1;
   *slot = -1;

   return 1;
}

int
pgagroal_management_read_payload(int socket, signed char id, int* payload_i, char** payload_s)
{
   int newfd, nr, status;
   char *ptr;
   char* s = NULL;
   char buf2[2];
   char buf4[4];
   struct cmsghdr *cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;

   *payload_i = -1;
   *payload_s = NULL;

   switch (id)
   {
      case MANAGEMENT_TRANSFER_CONNECTION:
         status = -1;
         newfd = -1;

         iov[0].iov_base = buf2;
         iov[0].iov_len = 2;
         msg.msg_iov = iov;
         msg.msg_iovlen = 1;
         msg.msg_name = NULL;
         msg.msg_namelen = 0;

         cmptr = malloc(CMSG_LEN(sizeof(int)));

         msg.msg_control    = cmptr;
         msg.msg_controllen = CMSG_LEN(sizeof(int));
         if ((nr = recvmsg(socket, &msg, 0)) < 0)
         {
            goto error;
         }
         else if (nr == 0)
         {
            /* TODO */
            return -1;
         }

         /*
          * See if this is the final data with null & status.  Null
          * is next to last byte of buffer; status byte is last byte.
          * Zero status means there is a file descriptor to receive.
          */
         for (ptr = buf2; ptr < &buf2[nr]; )
         {
            if (*ptr++ == 0)
            {
               status = *ptr & 0xFF;
               if (status == 0)
               {
                  newfd = *(int *)CMSG_DATA(cmptr);
               }
               else
               {
                  newfd = -status;
               }
               nr -= 2;
            }
         }

         *payload_i = newfd;

         free(cmptr);
         break;
      case MANAGEMENT_FLUSH:
      case MANAGEMENT_KILL_CONNECTION:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         *payload_i = pgagroal_read_int32(&buf4);
         break;
      case MANAGEMENT_ENABLEDB:
      case MANAGEMENT_DISABLEDB:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         *payload_i = pgagroal_read_int32(&buf4);

         s = malloc(*payload_i + 1);
         memset(s, 0, *payload_i + 1);
         if (read_complete(NULL, socket, s, *payload_i))
         {
            goto error;
         }
         *payload_s = s;
         break;
      case MANAGEMENT_RESET_SERVER:
         s = malloc(MISC_LENGTH);
         memset(s, 0, MISC_LENGTH);
         if (read_complete(NULL, socket, s, MISC_LENGTH))
         {
            goto error;
         }
         *payload_s = s;
         break;
      case MANAGEMENT_RETURN_CONNECTION:
      case MANAGEMENT_GRACEFULLY:
      case MANAGEMENT_STOP:
      case MANAGEMENT_CANCEL_SHUTDOWN:
      case MANAGEMENT_STATUS:
      case MANAGEMENT_DETAILS:
      case MANAGEMENT_RESET:
         break;
      default:
         goto error;
         break;
   }   

   return 0;

error:

   if (cmptr)
      free(cmptr);
   
   return 1;
}

int
pgagroal_management_transfer_connection(void* shmem, int32_t slot)
{
   int fd;
   struct configuration* config;
   struct cmsghdr *cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf[2]; /* send_fd()/recv_fd() 2-byte protocol */

   config = (struct configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      ZF_LOGW("pgagroal_management_transfer_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_TRANSFER_CONNECTION, slot))
   {
      ZF_LOGW("pgagroal_management_transfer_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   /* Write file descriptor */
   iov[0].iov_base = buf;
   iov[0].iov_len  = 2;
   msg.msg_iov     = iov;
   msg.msg_iovlen  = 1;
   msg.msg_name    = NULL;
   msg.msg_namelen = 0;

   cmptr = malloc(CMSG_LEN(sizeof(int)));
   cmptr->cmsg_level  = SOL_SOCKET;
   cmptr->cmsg_type   = SCM_RIGHTS;
   cmptr->cmsg_len    = CMSG_LEN(sizeof(int));
   msg.msg_control    = cmptr;
   msg.msg_controllen = CMSG_LEN(sizeof(int));
   *(int *)CMSG_DATA(cmptr) = config->connections[slot].fd;
   buf[1] = 0; /* zero status means OK */
   buf[0] = 0; /* null byte flag to recv_fd() */

   if (sendmsg(fd, &msg, 0) != 2)
   {
      goto error;
   }
   
   free(cmptr);
   pgagroal_disconnect(fd);

   return 0;

error:
   free(cmptr);
   pgagroal_disconnect(fd);
   pgagroal_kill_connection(shmem, slot);

   return 1;
}

int
pgagroal_management_return_connection(void* shmem, int32_t slot)
{
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      ZF_LOGW("pgagroal_management_return_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_RETURN_CONNECTION, slot))
   {
      ZF_LOGW("pgagroal_management_return_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_disconnect(fd);

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_kill_connection(void* shmem, int32_t slot, int socket)
{
   int fd;
   char buf[4];
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      ZF_LOGW("pgagroal_management_kill_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_KILL_CONNECTION, slot))
   {
      ZF_LOGW("pgagroal_management_kill_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, socket);
   if (write_complete(NULL, fd, &buf, sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_kill_connection: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   pgagroal_disconnect(fd);

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_flush(SSL* ssl, int fd, int32_t mode)
{
   char buf[4];

   if (write_header(ssl, fd, MANAGEMENT_FLUSH, -1))
   {
      ZF_LOGW("pgagroal_management_flush: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, mode);
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }
   
   return 0;

error:

   return 1;
}

int
pgagroal_management_enabledb(SSL* ssl, int fd, char* database)
{
   char buf[4];

   if (write_header(ssl, fd, MANAGEMENT_ENABLEDB, -1))
   {
      ZF_LOGW("pgagroal_management_enabledb: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, strlen(database));
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_enabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, fd, database, strlen(database)))
   {
      ZF_LOGW("pgagroal_management_enabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_disabledb(SSL* ssl, int fd, char* database)
{
   char buf[4];

   if (write_header(ssl, fd, MANAGEMENT_DISABLEDB, -1))
   {
      ZF_LOGW("pgagroal_management_disabledb: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, strlen(database));
   if (write_complete(ssl, fd, &buf, sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_disabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, fd, database, strlen(database)))
   {
      ZF_LOGW("pgagroal_management_disabledb: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_gracefully(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_GRACEFULLY, -1))
   {
      ZF_LOGW("pgagroal_management_gracefully: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_stop(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_STOP, -1))
   {
      ZF_LOGW("pgagroal_management_stop: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_cancel_shutdown(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_CANCEL_SHUTDOWN, -1))
   {
      ZF_LOGW("pgagroal_management_cancel_shutdown: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_status(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_STATUS, -1))
   {
      ZF_LOGW("pgagroal_management_status: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_status(SSL* ssl, int socket)
{
   char buf[16];
   char disabled[NUMBER_OF_DISABLED][MAX_DATABASE_LENGTH];
   int status;
   int active;
   int total;
   int max;

   memset(&buf, 0, sizeof(buf));
   memset(&disabled, 0, sizeof(disabled));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_read_status: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (read_complete(ssl, socket, &disabled[0], sizeof(disabled)))
   {
      ZF_LOGW("pgagroal_management_read_status: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   status = pgagroal_read_int32(&buf);
   active = pgagroal_read_int32(&(buf[4]));
   total = pgagroal_read_int32(&(buf[8]));
   max = pgagroal_read_int32(&(buf[12]));

   printf("Status:              %s\n", (status == 1 ? "Running" : "Graceful shutdown"));
   printf("Active connections:  %d\n", active);
   printf("Total connections:   %d\n", total);
   printf("Max connections:     %d\n", max);

   for (int i = 0; i < NUMBER_OF_DISABLED; i++)
   {
      if (strcmp(disabled[i], ""))
      {
         if (!strcmp(disabled[i], "*"))
         {
            printf("Disabled database:   ALL\n");
         }
         else
         {
            printf("Disabled database:   %s\n", disabled[i]);
         }
      }
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_status(int socket, bool graceful, void* shmem)
{
   char buf[16];
   int active;
   int total;
   struct configuration* config;

   memset(&buf, 0, sizeof(buf));
   active = 0;
   total = 0;

   config = (struct configuration*)shmem;

   if (!graceful)
   {
      pgagroal_write_int32(&buf, 1);
   }
   else
   {
      pgagroal_write_int32(&buf, 2);
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);
      switch (state)
      {
         case STATE_IN_USE:
            active++;
         case STATE_INIT:
         case STATE_FREE:
         case STATE_GRACEFULLY:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            total++;
            break;
         default:
            break;
      }
   }

   pgagroal_write_int32(&(buf[4]), active);
   pgagroal_write_int32(&(buf[8]), total);
   pgagroal_write_int32(&(buf[12]), config->max_connections);

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_write_status: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(NULL, socket, &config->disabled, sizeof(config->disabled)))
   {
      ZF_LOGW("pgagroal_management_write_status: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_details(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_DETAILS, -1))
   {
      ZF_LOGW("pgagroal_management_details: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_details(SSL* ssl, int socket)
{
   char header[12 + MAX_NUMBER_OF_CONNECTIONS];
   int max_connections = 0;
   int limits = 0;
   int servers = 0;

   memset(&header, 0, sizeof(header));

   if (read_complete(ssl, socket, &header[0], sizeof(header)))
   {
      ZF_LOGW("pgagroal_management_read_details: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   max_connections = pgagroal_read_int32(&header);
   limits = pgagroal_read_int32(&(header[4]));
   servers = pgagroal_read_int32(&(header[8]));

   for (int i = 0; i < servers; i++)
   {
      char server[5 + MISC_LENGTH + MISC_LENGTH];
      signed char state;

      memset(&server, 0, sizeof(server));

      if (read_complete(ssl, socket, &server[0], sizeof(server)))
      {
         ZF_LOGW("pgagroal_management_read_details: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      state = pgagroal_read_byte(&(server[MISC_LENGTH + MISC_LENGTH + 4]));

      printf("---------------------\n");
      printf("Server:              %s\n", pgagroal_read_string(&(server[0])));
      printf("Host:                %s\n", pgagroal_read_string(&(server[MISC_LENGTH])));
      printf("Port:                %d\n", pgagroal_read_int32(&(server[MISC_LENGTH + MISC_LENGTH])));

      switch (state)
      {
         case SERVER_NOTINIT:
            printf("State:               Not init\n");
            break;
         case SERVER_NOTINIT_PRIMARY:
            printf("State:               Not init (primary)\n");
            break;
         case SERVER_PRIMARY:
            printf("State:               Primary\n");
            break;
         case SERVER_REPLICA:
            printf("State:               Replica\n");
            break;
         case SERVER_FAILOVER:
            printf("State:               Failover\n");
            break;
         case SERVER_FAILED:
            printf("State:               Failed\n");
            break;
         default:
            printf("State:               %d\n", state);
            break;
      }
   }

   printf("---------------------\n");

   for (int i = 0; i < limits; i++)
   {
      char limit[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH];

      memset(&limit, 0, sizeof(limit));

      if (read_complete(ssl, socket, &limit[0], sizeof(limit)))
      {
         ZF_LOGW("pgagroal_management_read_details: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      printf("Database:            %s\n", pgagroal_read_string(&(limit[16])));
      printf("Username:            %s\n", pgagroal_read_string(&(limit[16 + MAX_DATABASE_LENGTH])));
      printf("Active connections:  %d\n", pgagroal_read_int32(&(limit)));
      printf("Max connections:     %d\n", pgagroal_read_int32(&(limit[4])));
      printf("Initial connections: %d\n", pgagroal_read_int32(&(limit[8])));
      printf("Min connections:     %d\n", pgagroal_read_int32(&(limit[12])));
      printf("---------------------\n");
   }

   for (int i = 0; i < max_connections; i++)
   {
      char details[12 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH + MAX_APPLICATION_NAME];
      signed char state;
      long time;
      time_t t;
      char ts[20] = {0};
      int pid;
      char p[10] = {0};

      memset(&details, 0, sizeof(details));

      if (read_complete(ssl, socket, &details[0], sizeof(details)))
      {
         ZF_LOGW("pgagroal_management_read_details: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      state = (signed char)header[12 + i];
      time = pgagroal_read_long(&(details[0]));
      pid = pgagroal_read_int32(&(details[8]));

      t = time;
      strftime(ts, 20, "%Y-%m-%d %H:%M:%S", localtime(&t));

      sprintf(p, "%d", pid);

      printf("Connection %4d:     %-15s %-19s %-6s %s %s %s\n",
             i,
             pgagroal_get_state_string(state),
             time > 0 ? ts : "",
             pid > 0 ? p : "",
             pgagroal_read_string(&(details[12])),
             pgagroal_read_string(&(details[12 + MAX_DATABASE_LENGTH])),
             pgagroal_read_string(&(details[12 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH])));
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_details(int socket, void* shmem)
{
   char header[12 + MAX_NUMBER_OF_CONNECTIONS];
   struct configuration* config;

   config = (struct configuration*)shmem;

   memset(&header, 0, sizeof(header));

   pgagroal_write_int32(header, config->max_connections);
   pgagroal_write_int32(header + 4, config->number_of_limits);
   pgagroal_write_int32(header + 8, config->number_of_servers);

   for (int i = 0; i < config->max_connections; i++)
   {
      signed char state = atomic_load(&config->states[i]);
      header[12 + i] = (char)state;
   }

   if (write_complete(NULL, socket, header, sizeof(header)))
   {
      ZF_LOGW("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      char server[5 + MISC_LENGTH + MISC_LENGTH];

      memset(&server, 0, sizeof(server));

      pgagroal_write_string(server, config->servers[i].name);
      pgagroal_write_string(server + MISC_LENGTH, config->servers[i].host);
      pgagroal_write_int32(server + MISC_LENGTH + MISC_LENGTH, config->servers[i].port);
      pgagroal_write_byte(server + MISC_LENGTH + MISC_LENGTH + 4, atomic_load(&config->servers[i].state));

      if (write_complete(NULL, socket, server, sizeof(server)))
      {
         ZF_LOGW("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   for (int i = 0; i < config->number_of_limits; i++)
   {
      char limit[16 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH];

      memset(&limit, 0, sizeof(limit));

      pgagroal_write_int32(limit, atomic_load(&config->limits[i].active_connections));
      pgagroal_write_int32(limit + 4, config->limits[i].max_size);
      pgagroal_write_int32(limit + 8, config->limits[i].initial_size);
      pgagroal_write_int32(limit + 12, config->limits[i].min_size);
      pgagroal_write_string(limit + 16, config->limits[i].database);
      pgagroal_write_string(limit + 16 + MAX_DATABASE_LENGTH, config->limits[i].username);

      if (write_complete(NULL, socket, &limit, sizeof(limit)))
      {
         ZF_LOGW("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      char details[12 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH + MAX_APPLICATION_NAME];

      memset(&details, 0, sizeof(details));

      pgagroal_write_long(details, (long)config->connections[i].timestamp);
      pgagroal_write_int32(details + 8, (int)config->connections[i].pid);

      pgagroal_write_string(details + 12, config->connections[i].database);
      pgagroal_write_string(details + 12 + MAX_DATABASE_LENGTH, config->connections[i].username);
      pgagroal_write_string(details + 12 + MAX_DATABASE_LENGTH + MAX_USERNAME_LENGTH, config->connections[i].appname);

      if (write_complete(NULL, socket, &details, sizeof(details)))
      {
         ZF_LOGW("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_isalive(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_ISALIVE, -1))
   {
      ZF_LOGW("pgagroal_management_isalive: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_isalive(SSL* ssl, int socket, int* status)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_read_isalive: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *status = pgagroal_read_int32(&buf);

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_isalive(int socket, bool gracefully, void* shmem)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (!gracefully)
   {
      pgagroal_write_int32(buf, 1);
   }
   else
   {
      pgagroal_write_int32(buf, 2);
   }

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      ZF_LOGW("pgagroal_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_reset(SSL* ssl, int fd)
{
   if (write_header(ssl, fd, MANAGEMENT_RESET, -1))
   {
      ZF_LOGW("pgagroal_management_reset: write: %d", fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_reset_server(SSL* ssl, int fd, char* server)
{
   char name[MISC_LENGTH];

   if (write_header(ssl, fd, MANAGEMENT_RESET_SERVER, -1))
   {
      ZF_LOGW("pgagroal_management_reset_server: write: %d", fd);
      errno = 0;
      goto error;
   }

   memset(&name[0], 0, MISC_LENGTH);
   memcpy(&name[0], server, strlen(server));

   if (write_complete(ssl, fd, &name[0], sizeof(name)))
   {
      ZF_LOGW("pgagroal_management_reset_server_: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      goto error;
   }
   else if (r < needs)
   {
      /* Sleep for 10ms */
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 10000000L;
      nanosleep(&ts, NULL);

      ZF_LOGV("Got: %ld, needs: %ld", r, needs);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         ZF_LOGD("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   } while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         ZF_LOGD("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               ZF_LOGE("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               ZF_LOGE("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   } while (keep_write);

   return 1;
}

static int
write_header(SSL* ssl, int fd, signed char type, int slot)
{
   char header[MANAGEMENT_HEADER_SIZE];

   pgagroal_write_byte(&(header), type);
   pgagroal_write_int32(&(header[1]), slot);

   return write_complete(ssl, fd, &(header), MANAGEMENT_HEADER_SIZE);
}
