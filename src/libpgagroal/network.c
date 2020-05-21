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
#include <network.h>

#define ZF_LOG_TAG "network"
#include <zf_log.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static int bind_host(const char* hostname, int port, void* shmem, int** fds, int* length);

/**
 *
 */
int
pgagroal_bind(const char* hostname, int port, void* shmem, int** fds, int* length)
{
   struct ifaddrs *ifaddr, *ifa;
   struct sockaddr_in *sa4;
   struct sockaddr_in6 *sa6;
   char addr[50];
   int* star_fds = NULL;
   int star_length = 0;

   if (!strcmp("*", hostname))
   {
      if (getifaddrs(&ifaddr) == -1)
      {
         ZF_LOGW("getifaddrs: %s", strerror(errno));
         errno = 0;
         return 1;
      }

      for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
      {
         if (ifa->ifa_addr != NULL &&
             (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) &&
             (ifa->ifa_flags & IFF_UP))
         {
            int* new_fds = NULL;
            int new_length = 0;

            memset(addr, 0, sizeof(addr));

            if (ifa->ifa_addr->sa_family == AF_INET)
            {
               sa4 = (struct sockaddr_in*) ifa->ifa_addr;
               inet_ntop(AF_INET, &sa4->sin_addr, addr, sizeof(addr));
            }
            else
            {
               sa6 = (struct sockaddr_in6 *) ifa->ifa_addr;
               inet_ntop(AF_INET6, &sa6->sin6_addr, addr, sizeof(addr));
            }

            if (bind_host(addr, port, shmem, &new_fds, &new_length))
            {
               free(new_fds);
               continue;
            }

            if (star_fds == NULL)
            {
               star_fds = malloc(new_length * sizeof(int));
               memcpy(star_fds, new_fds, new_length * sizeof(int));
               star_length = new_length;
            }
            else
            {
               star_fds = realloc(star_fds, (star_length + new_length) * sizeof(int));
               memcpy(star_fds + star_length, new_fds, new_length * sizeof(int));
               star_length += new_length;
            }

            free(new_fds);
         }
      }

      *fds = star_fds;
      *length = star_length;

      freeifaddrs(ifaddr);
      return 0;
   }

   return bind_host(hostname, port, shmem, fds, length);
}

/**
 *
 */
int
pgagroal_bind_unix_socket(const char* directory, void* shmem, int *fd)
{
   struct sockaddr_un addr;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if ((*fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
   {
      ZF_LOGE("pgagroal_bind_unix_socket: socket: %s %s", directory, strerror(errno));
      errno = 0;
      goto error;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   if (!directory)
   {
      directory = "/tmp/.s.pgagroal";
   }

   strncpy(addr.sun_path, directory, sizeof(addr.sun_path) - 1);
   unlink(directory);

   if (bind(*fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
   {
      ZF_LOGE("pgagroal_bind_unix_socket: bind: %s %s", directory, strerror(errno));
      errno = 0;
      goto error;
   }

   if (listen(*fd, config->backlog) == -1)
   {
      ZF_LOGE("pgagroal_bind_unix_socket: listen: %s %s", directory, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

/**
 *
 */
int
pgagroal_remove_unix_socket(const char* directory)
{
   unlink(directory);

   return 0;
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
   char sport[5];
   int error = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   memset(&sport, 0, sizeof(sport));
   sprintf(&sport[0], "%d", port);

   /* Connect to server */
   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;

   if ((rv = getaddrinfo(hostname, &sport[0], &hints, &servinfo)) != 0)
   {
      ZF_LOGD("getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
   }

   *fd = -1;

   /* Loop through all the results and connect to the first we can */
   for (p = servinfo; *fd == -1 && p != NULL; p = p->ai_next)
   {
      if ((*fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      {
         error = errno;
         errno = 0;
      }

      if (*fd != -1)
      {
         if (config != NULL && config->keep_alive)
         {
            if (setsockopt(*fd, SOL_SOCKET, SO_KEEPALIVE, &yes, optlen) == -1)
            {
               error = errno;
               pgagroal_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }
         }

         if (config != NULL && config->nodelay)
         {
            if (setsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &yes, optlen) == -1)
            {
               error = errno;
               pgagroal_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }
         }

         if (config != NULL)
         {
            if (setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &config->buffer_size, optlen) == -1)
            {
               error = errno;
               pgagroal_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }

            if (setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &config->buffer_size, optlen) == -1)
            {
               error = errno;
               pgagroal_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }
         }

         if (connect(*fd, p->ai_addr, p->ai_addrlen) == -1)
         {
            error = errno;
            pgagroal_disconnect(*fd);
            errno = 0;
            *fd = -1;
            continue;
         }
      }
   }

   if (*fd == -1)
   {
      goto error;
   }

   freeaddrinfo(servinfo);

   /* Set O_NONBLOCK on the socket */
   if (config != NULL && config->non_blocking)
   {
      pgagroal_socket_nonblocking(*fd, true);
   }

   return 0;

error:

   ZF_LOGD("pgagroal_connect: %s", strerror(error));
   return 1;
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
      ZF_LOGW("pgagroal_connect_unix_socket: socket: %s %s", directory, strerror(errno));
      errno = 0;
      return 1;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   strncpy(addr.sun_path, directory, sizeof(addr.sun_path) - 1);

   if (connect(*fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
   {
      ZF_LOGW("pgagroal_connect_unix_socket: connect: %s %s", directory, strerror(errno));
      errno = 0;
      return 1;
   }

   return 0;
}

bool
pgagroal_socket_isvalid(int fd)
{
   int error = 0;
   socklen_t length;
   int r;

   r = fcntl(fd, F_GETFL);

   if (r == -1)
   {
      errno = 0;
      return false;
   }

   length = sizeof(error);
   r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);

   if (r != 0 || error != 0)
   {
      errno = 0;
      return false;
   }

   return true;
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

void
pgagroal_get_address(struct sockaddr *sa, char* address, size_t length)
{
   if (sa->sa_family == AF_INET)
   {
      inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), address, length);
   }
   else
   {
      inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), address, length);
   }
}

int
pgagroal_socket_nonblocking(int fd, bool value)
{
   int flags;

   flags = fcntl(fd, F_GETFL);

   if (value)
   {
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
   }
   else
   {
      fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
   }

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
         ZF_LOGW("tcp_nodelay: %d %s", fd, strerror(errno));
         errno = 0;
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
      ZF_LOGW("socket_buffers: SO_RCVBUF %d %s", fd, strerror(errno));
      errno = 0;
      return 1;
   }

   if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &config->buffer_size, optlen) == -1)
   {
      ZF_LOGW("socket_buffers: SO_SNDBUF %d %s", fd, strerror(errno));
      errno = 0;
      return 1;
   }

   return 0;
}

/**
 *
 */
static int
bind_host(const char* hostname, int port, void* shmem, int** fds, int* length)
{
   int *result = NULL;
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
      ZF_LOGE("getaddrinfo: %s:%d (%s)", hostname, port, gai_strerror(rv));
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
         ZF_LOGD("server: socket: %s:%d (%s)", hostname, port, strerror(errno));
         continue;
      }

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
      {
         ZF_LOGD("server: so_reuseaddr: %d %s", sockfd, strerror(errno));
         pgagroal_disconnect(sockfd);
         continue;
      }

      if (config->non_blocking)
      {
         if (pgagroal_socket_nonblocking(sockfd, true))
         {
            pgagroal_disconnect(sockfd);
            continue;
         }
      }

      if (pgagroal_socket_buffers(sockfd, shmem))
      {
         pgagroal_disconnect(sockfd);
         continue;
      }

      if (pgagroal_tcp_nodelay(sockfd, shmem))
      {
         pgagroal_disconnect(sockfd);
         continue;
      }

      if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
      {
         pgagroal_disconnect(sockfd);
         ZF_LOGD("server: bind: %s:%d (%s)", hostname, port, strerror(errno));
         continue;
      }

      if (listen(sockfd, config->backlog) == -1)
      {
         pgagroal_disconnect(sockfd);
         ZF_LOGD("server: listen: %s:%d (%s)", hostname, port, strerror(errno));
         continue;
      }

      *(result + index) = sockfd;
      index++;
   }

   freeaddrinfo(servinfo);

   if (index == 0)
   {
      free(result);
      return 1;
   }

   *fds = result;
   *length = index;

   return 0;
}
