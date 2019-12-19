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

#define MANAGEMENT_HEADER_SIZE 5

static int write_header(void* shmem, signed char type, int slot, int* fd);

int
pgagroal_management_read_header(int socket, signed char* id, int32_t* slot)
{
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t r;

   r = read(socket, &header, MANAGEMENT_HEADER_SIZE);
   if (r == -1)
   {
      ZF_LOGW("pgagroal_management_read_header: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *id = pgagroal_read_byte(&(header));
   *slot = pgagroal_read_int32(&(header[1]));
   
   return 0;

error:

   return 1;
}

int
pgagroal_management_read_payload(int socket, signed char id, int* payload)
{
   int newfd, nr, status;
   ssize_t r;
   char *ptr;
   char buf2[2];
   char buf4[4];
   struct cmsghdr *cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;

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

         *payload = newfd;

         free(cmptr);
         break;
      case MANAGEMENT_FLUSH:
         r = read(socket, &buf4, 4 * sizeof(char));
         if (r == -1)
         {
            goto error;
         }
         *payload = pgagroal_read_int32(&buf4);
         break;
      case MANAGEMENT_RETURN_CONNECTION:
      case MANAGEMENT_KILL_CONNECTION:
      case MANAGEMENT_GRACEFULLY:
      case MANAGEMENT_STOP:
      case MANAGEMENT_STATUS:
      case MANAGEMENT_DETAILS:
         *payload = -1;
         break;
      default:
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
   ssize_t w;
   int fd;
   struct configuration* config;
   struct cmsghdr *cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf[2]; /* send_fd()/recv_fd() 2-byte protocol */

   config = (struct configuration*)shmem;

   w = write_header(shmem, MANAGEMENT_TRANSFER_CONNECTION, slot, &fd);
   if (w == -1)
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
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_RETURN_CONNECTION, slot, &fd);
   if (w == -1)
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
pgagroal_management_kill_connection(void* shmem, int32_t slot)
{
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_KILL_CONNECTION, slot, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_kill_connection: write: %d", fd);
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
pgagroal_management_flush(void* shmem, int32_t mode)
{
   ssize_t w;
   int fd;
   char buf[4];

   w = write_header(shmem, MANAGEMENT_FLUSH, -1, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_flush: write: %d", fd);
      errno = 0;
      goto error;
   }

   pgagroal_write_int32(&buf, mode);
   w = write(fd, &buf, 4 * sizeof(char));
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
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
pgagroal_management_gracefully(void* shmem)
{
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_GRACEFULLY, -1, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_gracefully: write: %d", fd);
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
pgagroal_management_stop(void* shmem)
{
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_STOP, -1, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_stop: write: %d", fd);
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
pgagroal_management_status(void* shmem, int* socket)
{
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_STATUS, -1, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_status: write: %d", fd);
      errno = 0;
      goto error;
   }

   *socket = fd;

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_read_status(int socket)
{
   char buf[16];
   ssize_t r;
   int status;
   int active;
   int total;
   int max;

   memset(&buf, 0, sizeof(buf));

   r = read(socket, &buf, sizeof(buf));
   if (r == -1)
   {
      ZF_LOGW("pgagroal_management_read_status: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   status = pgagroal_read_int32(&buf);
   active = pgagroal_read_int32(&(buf[4]));
   total = pgagroal_read_int32(&(buf[8]));
   max = pgagroal_read_int32(&(buf[12]));

   printf("Status:             %s\n", (status == 1 ? "Running" : "Graceful shutdown"));
   printf("Active connections: %d\n", active);
   printf("Total connections:  %d\n", total);
   printf("Max connections:    %d\n", max);

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_status(bool graceful, void* shmem, int socket)
{
   char buf[16];
   int active;
   int total;
   ssize_t w;
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

   w = write(socket, &buf, sizeof(buf));
   if (w == -1)
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
pgagroal_management_details(void* shmem, int* socket)
{
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_DETAILS, -1, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_details: write: %d", fd);
      errno = 0;
      goto error;
   }

   *socket = fd;

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_read_details(int socket)
{
   char buf[MAX_BUFFER_SIZE];
   ssize_t r;
   int size;
   int length;
   int offset;
   int active;
   int total;
   int max;

   memset(&buf, 0, sizeof(buf));

   r = read(socket, &buf, sizeof(buf));
   if (r == -1)
   {
      ZF_LOGW("pgagroal_management_read_details: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   size = pgagroal_read_int32(&buf);
   offset = 4;

   for (int i = 0; i < size; i++)
   {
      char* database = pgagroal_read_string(&(buf[offset]));
      length = strlen(database) + 1;
      offset += length;

      char* username = pgagroal_read_string(&(buf[offset]));
      length = strlen(username) + 1;
      offset += length;

      active = pgagroal_read_int32(&(buf[offset]));
      offset += 4;

      total = pgagroal_read_int32(&(buf[offset]));
      offset += 4;

      max = pgagroal_read_int32(&(buf[offset]));
      offset += 4;

      if (max > 0)
      {
         printf("--------------------\n");
         printf("Database:           %s\n", database);
         printf("Username:           %s\n", username);
         printf("Active connections: %d\n", active);
         printf("Total connections:  %d\n", total);
         printf("Max connections:    %d\n", max);
      }
   }

   size = pgagroal_read_int32(&(buf[offset]));
   offset += 4;

   printf("--------------------\n");
   for (int i = 0; i < size; i++)
   {
      signed char state;
      long time;
      time_t t;
      char ts[20] = {0};
      int pid;
      char p[10] = {0};

      state = pgagroal_read_byte(&(buf[offset]));
      offset += 1;

      time = pgagroal_read_long(&(buf[offset]));
      offset += 8;

      pid = pgagroal_read_int32(&(buf[offset]));
      offset += 4;

      t = time;
      strftime(ts, 20, "%Y-%m-%d %H:%M:%S", localtime(&t));

      sprintf(p, "%d", pid);

      printf("Connection %4d:    %-15s %-19s %s\n",
             i,
             pgagroal_get_state_string(state),
             time > 0 ? ts : "",
             pid > 0 ? p : "");
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_write_details(void* shmem, int socket)
{
   ssize_t w;
   struct configuration* config;
   int offset;
   int buffer_size;
   char* buf = NULL;

   config = (struct configuration*)shmem;

   buffer_size = sizeof(int) + (config->max_connections * (sizeof(signed char) + sizeof(long) + sizeof(int)));

   if (config->number_of_limits > 0)
   {
      int length;
      int active[config->number_of_limits + 1];
      int total[config->number_of_limits + 1];
      int max = config->max_connections;

      buffer_size += sizeof(int);

      for (int i = 0; i < config->number_of_limits; i++)
      {
         buffer_size += strlen(config->limits[i].database) + 1;
         buffer_size += strlen(config->limits[i].username) + 1;
         buffer_size += sizeof(int);
         buffer_size += sizeof(int);
         buffer_size += sizeof(int);
      }

      buffer_size += strlen("all") + 1;
      buffer_size += strlen("all") + 1;
      buffer_size += sizeof(int);
      buffer_size += sizeof(int);
      buffer_size += sizeof(int);

      buf = malloc(buffer_size);

      memset(buf, 0, buffer_size);
      memset(&active, 0, sizeof(active));
      memset(&total, 0, sizeof(total));

      for (int i = 0; i < config->max_connections; i++)
      {
         int state = atomic_load(&config->states[i]);
         switch (state)
         {
            case STATE_IN_USE:
               active[config->connections[i].limit_rule + 1]++;
            case STATE_INIT:
            case STATE_FREE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
            case STATE_IDLE_CHECK:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               total[config->connections[i].limit_rule + 1]++;
               break;
            default:
               break;
         }
      }

      pgagroal_write_int32(buf, config->number_of_limits + 1);
      offset = 4;

      for (int i = 0; i < config->number_of_limits; i++)
      {
         length = strlen(config->limits[i].database);
         pgagroal_write_string(buf + offset, config->limits[i].database);
         offset += length + 1;

         length = strlen(config->limits[i].username);
         pgagroal_write_string(buf + offset, config->limits[i].username);
         offset += length + 1;

         pgagroal_write_int32(buf + offset, active[i + 1]);
         offset += 4;

         pgagroal_write_int32(buf + offset, total[i + 1]);
         offset += 4;

         pgagroal_write_int32(buf + offset, config->limits[i].max_connections);
         offset += 4;

         max -= config->limits[i].max_connections;
      }

      length = strlen("all");
      pgagroal_write_string(buf + offset, "all");
      offset += length + 1;

      length = strlen("all");
      pgagroal_write_string(buf + offset, "all");
      offset += length + 1;

      pgagroal_write_int32(buf + offset, active[0]);
      offset += 4;

      pgagroal_write_int32(buf + offset, total[0]);
      offset += 4;

      pgagroal_write_int32(buf + offset, max);
      offset += 4;
   }
   else
   {
      buffer_size += sizeof(int);
      buf = malloc(buffer_size);

      memset(buf, 0, buffer_size);

      pgagroal_write_int32(buf, 0);
      offset = 4;
   }

   pgagroal_write_int32(buf + offset, config->max_connections);
   offset += 4;

   for (int i = 0; i < config->max_connections; i++)
   {
      pgagroal_write_byte(buf + offset, atomic_load(&config->states[i]));
      offset += 1;

      pgagroal_write_long(buf + offset, (long)config->connections[i].timestamp);
      offset += 8;

      pgagroal_write_int32(buf + offset, (int)config->connections[i].pid);
      offset += 4;
   }

   w = write(socket, buf, buffer_size);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_write_details: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   free(buf);

   return 0;

error:

   free(buf);

   return 1;
}

int
pgagroal_management_isalive(void* shmem, int* socket)
{
   ssize_t w;
   int fd;

   w = write_header(shmem, MANAGEMENT_ISALIVE, -1, &fd);
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_isalive: write: %d", fd);
      errno = 0;
      goto error;
   }

   *socket = fd;

   return 0;

error:
   pgagroal_disconnect(fd);

   return 1;
}

int
pgagroal_management_read_isalive(int socket, int* status)
{
   char buf[MAX_BUFFER_SIZE];
   ssize_t r;

   memset(&buf, 0, sizeof(buf));

   r = read(socket, &buf, sizeof(buf));
   if (r == -1)
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
pgagroal_management_write_isalive(void* shmem, bool gracefully, int socket)
{
   ssize_t w;
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

   w = write(socket, buf, sizeof(buf));
   if (w == -1)
   {
      ZF_LOGW("pgagroal_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_header(void* shmem, signed char type, int slot, int* fd)
{
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   struct configuration* config;

   w = -1;
   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), type);
   pgagroal_write_int32(&(header[1]), slot);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, fd))
   {
      goto end;
   }

   ZF_LOGD("Write %d to %d (%d)", type, *fd, MANAGEMENT_HEADER_SIZE);
   ZF_LOGD("Slot %d FD %d ", slot, slot != -1 ? config->connections[slot].fd : -1);

   w = write(*fd, &(header), MANAGEMENT_HEADER_SIZE);

end:
   return w;
}
