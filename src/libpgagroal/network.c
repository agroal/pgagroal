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
#include <network.h>

#define ZF_LOG_TAG "network"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/**
 *
 */
int
pgagroal_bind(const char* hostname, int port, void* shmem, int** fds, int* length)
{
   int *result;
   int index, size;
   int sockfd;
   struct addrinfo hints, *servinfo, *addr;
   int yes = 1;
   int rv;
   char* sport;
   struct configuration* config;

   config = (struct configuration*)shmem;

   index = 0;
   size = 0;

   sport = malloc(5);
   memset(sport, 0, 5);
   sprintf(sport, "%d", port);

   /* Find all SOCK_STREAM addresses */
   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   if ((rv = getaddrinfo(hostname, sport, &hints, &servinfo)) != 0)
   {
      free(sport);
      ZF_LOGE("getaddrinfo: %s", gai_strerror(rv));
      return 1;
   }

   free(sport);
   
   for (addr = servinfo; addr != NULL; addr = addr->ai_next)
   {
      size++;
   }

   result = malloc(size * sizeof(int));
   memset(result, 0, size * sizeof(int));

   /* Loop through all the results and bind to the first we can */
   for (addr = servinfo; addr != NULL; addr = addr->ai_next)
   {
      if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
      {
         ZF_LOGD("server: socket: %s", strerror(errno));
         continue;
      }

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
      {
         ZF_LOGD("server: so_reuseaddr: %d %s", sockfd, strerror(errno));
         continue;
      }

      if (pgagroal_socket_nonblocking(sockfd, shmem))
      {
         continue;
      }

      if (pgagroal_socket_buffers(sockfd, shmem))
      {
         continue;
      }

      if (pgagroal_tcp_nodelay(sockfd, shmem))
      {
         continue;
      }

      if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
      {
         pgagroal_disconnect(sockfd);
         ZF_LOGD("server: bind: %s", strerror(errno));
         continue;
      }

      if (listen(sockfd, config->backlog) == -1)
      {
         pgagroal_disconnect(sockfd);
         ZF_LOGD("server: listen: %s", strerror(errno));
         continue;
      }

      *(result + index) = sockfd;
      index++;
   }

   freeaddrinfo(servinfo);

   if (index == 0)
   {
      return 1;
   }

   *fds = result;
   *length = index;

   return 0;
}

/**
 *
 */
int
pgagroal_bind_unix_socket(const char* directory, void* shmem)
{
   struct sockaddr_un addr;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
   {
      perror("socket error");
      exit(-1);
   }

   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   if (!directory)
   {
      directory = "/tmp/.s.pgagroal";
   }

   strncpy(addr.sun_path, directory, sizeof(addr.sun_path) - 1);
   unlink(directory);
   /*
   if (unlink(directory) == -1)
   {
      perror("unlink error");
   }
   */

   if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
   {
      perror("bind error");
      exit(-1);
   }

   if (listen(fd, config->backlog) == -1)
   {
      perror("listen error");
      exit(-1);
   }

   return fd;
}

/**
 *
 */
int
pgagroal_connect(void* shmem, const char* hostname, int port, int* fd)
{
   struct addrinfo hints, *servinfo, *p;
   int yes = 1;
   socklen_t optlen = sizeof(int);
   int rv;
   char* sport;
   struct configuration* config;

   config = (struct configuration*)shmem;

   sport = malloc(5);
   memset(sport, 0, 5);
   sprintf(sport, "%d", port);

   /* Connect to server */
   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;

   if ((rv = getaddrinfo(hostname, sport, &hints, &servinfo)) != 0) {
      free(sport);
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
   }

   free(sport);

   /* Loop through all the results and connect to the first we can */
   for (p = servinfo; p != NULL; p = p->ai_next)
   {
      if ((*fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      {
         perror("server: socket");
         return 1;
      }

      if (config->keep_alive)
      {
         if (setsockopt(*fd, SOL_SOCKET, SO_KEEPALIVE, &yes, optlen) == -1)
         {
            pgagroal_disconnect(*fd);
            perror("server: SO_KEEPALIVE");
            return 1;
         }
      }

      if (config->nodelay)
      {
         if (setsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &yes, optlen) == -1)
         {
            pgagroal_disconnect(*fd);
            perror("server: TCP_NODELAY");
            return 1;
         }
      }

      if (setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &config->buffer_size, optlen) == -1)
      {
         pgagroal_disconnect(*fd);
         perror("server: SO_RCVBUF");
         return 1;
      }

      if (setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &config->buffer_size, optlen) == -1)
      {
         pgagroal_disconnect(*fd);
         perror("server: SO_SNDBUF");
         return 1;
      }

      if (connect(*fd, p->ai_addr, p->ai_addrlen) == -1)
      {
         pgagroal_disconnect(*fd);
         perror("server: connect");
         return 1;
      }

      break;
   }

   if (p == NULL)
   {
      fprintf(stderr, "server: failed to connect\n");
      return 1;
   }

   /* inet_ntop(p->ai_family, pgagroal_get_sockaddr((struct sockaddr *)p->ai_addr), s, sizeof s); */

   freeaddrinfo(servinfo);

   /* Set O_NONBLOCK on the socket */
   if (config->non_blocking)
   {
      pgagroal_socket_nonblocking(*fd, shmem);
   }

   return 0;
}

/**
 *
 */
int
pgagroal_connect_unix_socket(const char* directory, int* fd)
{
   struct sockaddr_un addr;

   if ((*fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
   {
      perror("socket error");
      return 1;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   strncpy(addr.sun_path, directory, sizeof(addr.sun_path) - 1);

   if (connect(*fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
   {
      perror("connect error");
      return 1;
   }

   return 0;
}

/**
 *
 */
int
pgagroal_disconnect(int fd)
{
   if (fd == -1)
      return 1;

   return close(fd);
}

void*
pgagroal_get_sockaddr(struct sockaddr *sa)
{
   if (sa->sa_family == AF_INET)
   {
      return &(((struct sockaddr_in*)sa)->sin_addr);
   }

   return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int
pgagroal_is_ready(int fd)
{
   fd_set readmask;
   fd_set exceptmask;
   int fds;
   struct timeval timeout;
   struct timeval *timeoutp;

   timeout.tv_sec = 0;
   timeout.tv_usec = 1;
   timeoutp = &timeout;
   
   for (;;)
   {
      FD_ZERO(&readmask);
      FD_ZERO(&exceptmask);
      FD_SET(fd, &readmask);
      FD_SET(fd, &exceptmask);

      fds = select(fd + 1, &readmask, NULL, &exceptmask, timeoutp);
      if (fds == -1)
      {
         return -1;
      }
      else if (fds == 0)
      {
         return 0;
      }

      if (FD_ISSET(fd, &exceptmask))
      {
         return -1;
      }

      return 1;
   }

   return -1;
}

int
pgagroal_socket_nonblocking(int fd, void* shmem)
{
   int flags;

   flags = fcntl(fd, F_GETFL);
   fcntl(fd, F_SETFL, flags | O_NONBLOCK);

   return 0;
}

int
pgagroal_tcp_nodelay(int fd, void* shmem)
{
   struct configuration* config;
   int yes = 1;
   socklen_t optlen = sizeof(int);

   config = (struct configuration*)shmem;

   if (config->nodelay)
   {
      if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, optlen) == -1)
      {
         ZF_LOGD("tcp_nodelay: %d %s", fd, strerror(errno));
         return 1;
      }
   }

   return 0;
}

int
pgagroal_socket_buffers(int fd, void* shmem)
{
   struct configuration* config;
   socklen_t optlen = sizeof(int);

   config = (struct configuration*)shmem;

   if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &config->buffer_size, optlen) == -1)
   {
      ZF_LOGD("socket_buffers: SO_RCVBUF %d %s", fd, strerror(errno));
      return 1;
   }

   if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &config->buffer_size, optlen) == -1)
   {
      ZF_LOGD("socket_buffers: SO_SNDBUF %d %s", fd, strerror(errno));
      return 1;
   }

   return 0;
}
