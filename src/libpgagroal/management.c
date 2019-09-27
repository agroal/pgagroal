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

int
pgagroal_management_read_header(int socket, signed char* id, int32_t* slot)
{
   char header[MANAGEMENT_HEADER_SIZE];
   /* ssize_t r; */

   read(socket, &header, MANAGEMENT_HEADER_SIZE);

   *id = pgagroal_read_byte(&(header));
   *slot = pgagroal_read_int32(&(header[1]));
   
   return 0;
}

int
pgagroal_management_read_payload(int socket, signed char id, int* payload)
{
   int newfd, nr, status;
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
      case MANAGEMENT_RETURN_CONNECTION:
         *payload = 0;
         break;
      case MANAGEMENT_KILL_CONNECTION:
         *payload = 0;
         break;
      case MANAGEMENT_FLUSH:
         read(socket, &buf4, 4 * sizeof(char));
         *payload = pgagroal_read_int32(&buf4);
         break;
      case MANAGEMENT_GRACEFULLY:
         *payload = 0;
         break;
      case MANAGEMENT_STOP:
         *payload = 0;
         break;
      case MANAGEMENT_STATUS:
         *payload = 0;
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

void
pgagroal_management_free_payload(void* payload)
{
   if (payload)
      free(payload);
}

int
pgagroal_management_transfer_connection(void* shmem, int32_t slot)
{
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;
   struct cmsghdr *cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf[2]; /* send_fd()/recv_fd() 2-byte protocol */

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_TRANSFER_CONNECTION);
   pgagroal_write_int32(&(header[1]), slot);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }
   
   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_TRANSFER_CONNECTION, fd, MANAGEMENT_HEADER_SIZE);
   ZF_LOGD("Slot %d FD %d ", slot, config->connections[slot].fd);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_transfer_connection: write: %d %s", fd, strerror(errno));
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
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_RETURN_CONNECTION);
   pgagroal_write_int32(&(header[1]), slot);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }

   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_RETURN_CONNECTION, fd, MANAGEMENT_HEADER_SIZE);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_return_connection: write: %d %s", fd, strerror(errno));
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
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_KILL_CONNECTION);
   pgagroal_write_int32(&(header[1]), slot);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }
   
   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_KILL_CONNECTION, fd, MANAGEMENT_HEADER_SIZE);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_kill_connection: write: %d %s", fd, strerror(errno));
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
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;
   char buf[4];

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_FLUSH);
   pgagroal_write_int32(&(header[1]), 0);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }
   
   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_FLUSH, fd, MANAGEMENT_HEADER_SIZE);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
      goto error;
   }

   pgagroal_write_int32(&buf, mode);
   w = write(fd, &buf, 4 * sizeof(char));
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_flush: write: %d %s", fd, strerror(errno));
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
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_GRACEFULLY);
   pgagroal_write_int32(&(header[1]), 0);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }

   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_GRACEFULLY, fd, MANAGEMENT_HEADER_SIZE);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_gracefully: write: %d %s", fd, strerror(errno));
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
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_STOP);
   pgagroal_write_int32(&(header[1]), 0);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }

   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_STOP, fd, MANAGEMENT_HEADER_SIZE);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_stop: write: %d %s", fd, strerror(errno));
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
   char header[MANAGEMENT_HEADER_SIZE];
   ssize_t w;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgagroal_write_byte(&(header), MANAGEMENT_STATUS);
   pgagroal_write_int32(&(header[1]), 0);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, &fd))
   {
      goto error;
   }

   ZF_LOGD("Write %d to %d (%d)", MANAGEMENT_STATUS, fd, MANAGEMENT_HEADER_SIZE);

   w = write(fd, &(header), MANAGEMENT_HEADER_SIZE);
   if (w == -1)
   {
      ZF_LOGD("pgagroal_management_status: write: %d %s", fd, strerror(errno));
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
      ZF_LOGD("pgagroal_management_read_status: write: %d %s", socket, strerror(errno));
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
      ZF_LOGD("pgagroal_management_write_status: write: %d %s", socket, strerror(errno));
      goto error;
   }

   return 0;

error:

   return 1;
}
