/*
 * Copyright (C) 2024 The pgagroal community
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
#include <utils.h>
#include <server.h>

/* system */
#include <ev.h>
#include <execinfo.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <openssl/pem.h>
#include <sys/types.h>
#include <err.h>

#ifndef EVBACKEND_LINUXAIO
#define EVBACKEND_LINUXAIO 0x00000040U
#endif

#ifndef EVBACKEND_IOURING
#define EVBACKEND_IOURING  0x00000080U
#endif

extern char** environ;
#if defined(HAVE_LINUX) || defined(HAVE_OSX)
static bool env_changed = false;
static int max_process_title_size = 0;
#endif

int32_t
pgagroal_get_request(struct message* msg)
{
   if (msg == NULL || msg->data == NULL || msg->length < 8)
   {
      return -1;
   }

   return pgagroal_read_int32(msg->data + 4);
}

int
pgagroal_extract_username_database(struct message* msg, char** username, char** database, char** appname)
{
   int start, end;
   int counter = 0;
   signed char c;
   char** array = NULL;
   size_t size;
   char* un = NULL;
   char* db = NULL;
   char* an = NULL;

   *username = NULL;
   *database = NULL;
   *appname = NULL;

   /* We know where the parameters start, and we know that the message is zero terminated */
   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgagroal_read_byte(msg->data + i);
      if (c == 0)
      {
         counter++;
      }
   }

   array = (char**)malloc(sizeof(char*) * counter);

   counter = 0;
   start = 8;
   end = 8;

   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgagroal_read_byte(msg->data + i);
      end++;
      if (c == 0)
      {
         array[counter] = (char*)calloc(1, end - start);
         memcpy(array[counter], msg->data + start, end - start);

         start = end;
         counter++;
      }
   }

   for (int i = 0; i < counter; i++)
   {
      if (!strcmp(array[i], "user"))
      {
         size = strlen(array[i + 1]) + 1;
         un = calloc(1, size);
         memcpy(un, array[i + 1], size);

         *username = un;
      }
      else if (!strcmp(array[i], "database"))
      {
         size = strlen(array[i + 1]) + 1;
         db = calloc(1, size);
         memcpy(db, array[i + 1], size);

         *database = db;
      }
      else if (!strcmp(array[i], "application_name"))
      {
         size = strlen(array[i + 1]) + 1;
         an = calloc(1, size);
         memcpy(an, array[i + 1], size);

         *appname = an;
      }
   }

   if (*database == NULL)
   {
      *database = *username;
   }

   pgagroal_log_trace("Username: %s", *username);
   pgagroal_log_trace("Database: %s", *database);

   for (int i = 0; i < counter; i++)
   {
      free(array[i]);
   }
   free(array);

   return 0;
}

int
pgagroal_extract_message(char type, struct message* msg, struct message** extracted)
{
   int offset;
   int m_length;
   void* data = NULL;
   struct message* result = NULL;

   offset = 0;
   *extracted = NULL;

   while (result == NULL && offset < msg->length)
   {
      char t = (char)pgagroal_read_byte(msg->data + offset);

      if (type == t)
      {
         m_length = pgagroal_read_int32(msg->data + offset + 1);

         result = (struct message*)malloc(sizeof(struct message));
         data = (void*)malloc(1 + m_length);

         memcpy(data, msg->data + offset, 1 + m_length);

         result->kind = pgagroal_read_byte(data);
         result->length = 1 + m_length;
         result->max_length = 1 + m_length;
         result->data = data;

         *extracted = result;

         return 0;
      }
      else
      {
         offset += 1;
         offset += pgagroal_read_int32(msg->data + offset);
      }
   }

   return 1;
}

int
pgagroal_extract_error_message(struct message* msg, char** error)
{
   int max = 0;
   int offset = 5;
   signed char type;
   char* s = NULL;
   char* result = NULL;

   *error = NULL;

   if (msg->kind == 'E')
   {
      max = pgagroal_read_int32(msg->data + 1);

      while (result == NULL && offset < max)
      {
         type = pgagroal_read_byte(msg->data + offset);
         s = pgagroal_read_string(msg->data + offset + 1);

         if (type == 'M')
         {
            result = (char*)calloc(1, strlen(s) + 1);
            memcpy(result, s, strlen(s));

            *error = result;
         }

         offset += 1 + strlen(s) + 1;
      }
   }
   else
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

char*
pgagroal_get_state_string(signed char state)
{
   switch (state)
   {
      case STATE_NOTINIT:
         return "Not initialized";
      case STATE_INIT:
         return "Initializing";
      case STATE_FREE:
         return "Free";
      case STATE_IN_USE:
         return "Active";
      case STATE_GRACEFULLY:
         return "Graceful";
      case STATE_FLUSH:
         return "Flush";
      case STATE_IDLE_CHECK:
         return "Idle check";
      case STATE_MAX_CONNECTION_AGE:
         return "Max connection age check";
      case STATE_VALIDATION:
         return "Validating";
      case STATE_REMOVE:
         return "Removing";
   }

   return "Unknown";
}

signed char
pgagroal_read_byte(void* data)
{
   return (signed char) *((char*)data);
}

int16_t
pgagroal_read_int16(void* data)
{
   unsigned char bytes[] = {*((unsigned char*)data),
                            *((unsigned char*)(data + 1))};

   int16_t res = (int16_t)((bytes[0] << 8)) |
                 ((bytes[1]));

   return res;
}

int32_t
pgagroal_read_int32(void* data)
{
   unsigned char bytes[] = {*((unsigned char*)data),
                            *((unsigned char*)(data + 1)),
                            *((unsigned char*)(data + 2)),
                            *((unsigned char*)(data + 3))};

   int32_t res = (int32_t)((bytes[0] << 24)) |
                 ((bytes[1] << 16)) |
                 ((bytes[2] << 8)) |
                 ((bytes[3]));

   return res;
}

long
pgagroal_read_long(void* data)
{
   unsigned char bytes[] = {*((unsigned char*)data),
                            *((unsigned char*)(data + 1)),
                            *((unsigned char*)(data + 2)),
                            *((unsigned char*)(data + 3)),
                            *((unsigned char*)(data + 4)),
                            *((unsigned char*)(data + 5)),
                            *((unsigned char*)(data + 6)),
                            *((unsigned char*)(data + 7))};

   long res = (long)(((long)bytes[0]) << 56) |
              (((long)bytes[1]) << 48) |
              (((long)bytes[2]) << 40) |
              (((long)bytes[3]) << 32) |
              (((long)bytes[4]) << 24) |
              (((long)bytes[5]) << 16) |
              (((long)bytes[6]) << 8) |
              (((long)bytes[7]));

   return res;
}

char*
pgagroal_read_string(void* data)
{
   return (char*)data;
}

void
pgagroal_write_byte(void* data, signed char b)
{
   *((char*)(data)) = b;
}

void
pgagroal_write_int32(void* data, int32_t i)
{
   char* ptr = (char*)&i;

   *((char*)(data + 3)) = *ptr;
   ptr++;
   *((char*)(data + 2)) = *ptr;
   ptr++;
   *((char*)(data + 1)) = *ptr;
   ptr++;
   *((char*)(data)) = *ptr;
}

void
pgagroal_write_long(void* data, long l)
{
   char* ptr = (char*)&l;

   *((char*)(data + 7)) = *ptr;
   ptr++;
   *((char*)(data + 6)) = *ptr;
   ptr++;
   *((char*)(data + 5)) = *ptr;
   ptr++;
   *((char*)(data + 4)) = *ptr;
   ptr++;
   *((char*)(data + 3)) = *ptr;
   ptr++;
   *((char*)(data + 2)) = *ptr;
   ptr++;
   *((char*)(data + 1)) = *ptr;
   ptr++;
   *((char*)(data)) = *ptr;
}

void
pgagroal_write_string(void* data, char* s)
{
   memcpy(data, s, strlen(s));
}

bool
pgagroal_bigendian(void)
{
   short int word = 0x0001;
   char* b = (char*)&word;
   return (b[0] ? false : true);
}

unsigned int
pgagroal_swap(unsigned int i)
{
   return ((i << 24) & 0xff000000) |
          ((i << 8) & 0x00ff0000) |
          ((i >> 8) & 0x0000ff00) |
          ((i >> 24) & 0x000000ff);
}

void
pgagroal_libev_engines(void)
{
   unsigned int engines = ev_supported_backends();

   if (engines & EVBACKEND_SELECT)
   {
      pgagroal_log_debug("libev available: select");
   }
   if (engines & EVBACKEND_POLL)
   {
      pgagroal_log_debug("libev available: poll");
   }
   if (engines & EVBACKEND_EPOLL)
   {
      pgagroal_log_debug("libev available: epoll");
   }
   if (engines & EVBACKEND_LINUXAIO)
   {
      pgagroal_log_debug("libev available: linuxaio");
   }
   if (engines & EVBACKEND_IOURING)
   {
      pgagroal_log_debug("libev available: iouring");
   }
   if (engines & EVBACKEND_KQUEUE)
   {
      pgagroal_log_debug("libev available: kqueue");
   }
   if (engines & EVBACKEND_DEVPOLL)
   {
      pgagroal_log_debug("libev available: devpoll");
   }
   if (engines & EVBACKEND_PORT)
   {
      pgagroal_log_debug("libev available: port");
   }
}

unsigned int
pgagroal_libev(char* engine)
{
   unsigned int engines = ev_supported_backends();

   if (engine)
   {
      if (!strcmp("select", engine))
      {
         if (engines & EVBACKEND_SELECT)
         {
            return EVBACKEND_SELECT;
         }
         else
         {
            pgagroal_log_warn("libev not available: select");
         }
      }
      else if (!strcmp("poll", engine))
      {
         if (engines & EVBACKEND_POLL)
         {
            return EVBACKEND_POLL;
         }
         else
         {
            pgagroal_log_warn("libev not available: poll");
         }
      }
      else if (!strcmp("epoll", engine))
      {
         if (engines & EVBACKEND_EPOLL)
         {
            return EVBACKEND_EPOLL;
         }
         else
         {
            pgagroal_log_warn("libev not available: epoll");
         }
      }
      else if (!strcmp("linuxaio", engine))
      {
         return EVFLAG_AUTO;
      }
      else if (!strcmp("iouring", engine))
      {
         if (engines & EVBACKEND_IOURING)
         {
            return EVBACKEND_IOURING;
         }
         else
         {
            pgagroal_log_warn("libev not available: iouring");
         }
      }
      else if (!strcmp("devpoll", engine))
      {
         if (engines & EVBACKEND_DEVPOLL)
         {
            return EVBACKEND_DEVPOLL;
         }
         else
         {
            pgagroal_log_warn("libev not available: devpoll");
         }
      }
      else if (!strcmp("port", engine))
      {
         if (engines & EVBACKEND_PORT)
         {
            return EVBACKEND_PORT;
         }
         else
         {
            pgagroal_log_warn("libev not available: port");
         }
      }
      else if (!strcmp("auto", engine) || !strcmp("", engine))
      {
         return EVFLAG_AUTO;
      }
      else
      {
         pgagroal_log_warn("libev unknown option: %s", engine);
      }
   }

   return EVFLAG_AUTO;
}

char*
pgagroal_libev_engine(unsigned int val)
{
   switch (val)
   {
      case EVBACKEND_SELECT:
         return "select";
      case EVBACKEND_POLL:
         return "poll";
      case EVBACKEND_EPOLL:
         return "epoll";
      case EVBACKEND_LINUXAIO:
         return "linuxaio";
      case EVBACKEND_IOURING:
         return "iouring";
      case EVBACKEND_KQUEUE:
         return "kqueue";
      case EVBACKEND_DEVPOLL:
         return "devpoll";
      case EVBACKEND_PORT:
         return "port";
   }

   return "Unknown";
}

char*
pgagroal_get_home_directory(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_dir;
}

char*
pgagroal_get_user_name(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_name;
}

char*
pgagroal_get_password(void)
{
   char p[MAX_PASSWORD_LENGTH];
   struct termios oldt, newt;
   int i = 0;
   int c;
   char* result = NULL;

   memset(&p, 0, sizeof(p));

   tcgetattr(STDIN_FILENO, &oldt);
   newt = oldt;

   newt.c_lflag &= ~(ECHO);

   tcsetattr(STDIN_FILENO, TCSANOW, &newt);

   while ((c = getchar()) != '\n' && c != EOF && i < MAX_PASSWORD_LENGTH)
   {
      p[i++] = c;
   }
   p[i] = '\0';

   tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

   result = calloc(1, strlen(p) + 1);

   memcpy(result, &p, strlen(p));

   return result;
}

int
pgagroal_base64_encode(char* raw, int raw_length, char** encoded)
{
   BIO* b64_bio;
   BIO* mem_bio;
   BUF_MEM* mem_bio_mem_ptr;
   char* r = NULL;

   if (raw == NULL)
   {
      goto error;
   }

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);
   BIO_write(b64_bio, raw, raw_length);
   BIO_flush(b64_bio);

   BIO_get_mem_ptr(mem_bio, &mem_bio_mem_ptr);

   BIO_set_close(mem_bio, BIO_NOCLOSE);
   BIO_free_all(b64_bio);

   BUF_MEM_grow(mem_bio_mem_ptr, (*mem_bio_mem_ptr).length + 1);
   (*mem_bio_mem_ptr).data[(*mem_bio_mem_ptr).length] = '\0';

   r = calloc(1, strlen((*mem_bio_mem_ptr).data) + 1);
   memcpy(r, (*mem_bio_mem_ptr).data, strlen((*mem_bio_mem_ptr).data));

   BUF_MEM_free(mem_bio_mem_ptr);

   *encoded = r;

   return 0;

error:

   *encoded = NULL;

   return 1;
}

int
pgagroal_base64_decode(char* encoded, size_t encoded_length, char** raw, int* raw_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   size_t size;
   char* decoded;
   int index;

   if (encoded == NULL)
   {
      goto error;
   }

   size = (encoded_length * 3) / 4 + 1;
   decoded = calloc(1, size);

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_write(mem_bio, encoded, encoded_length);
   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

   index = 0;
   while (0 < BIO_read(b64_bio, decoded + index, 1))
   {
      index++;
   }

   BIO_free_all(b64_bio);

   *raw = decoded;
   *raw_length = index;

   return 0;

error:

   *raw = NULL;
   *raw_length = 0;

   return 1;
}

void
pgagroal_set_proc_title(int argc, char** argv, char* s1, char* s2)
{
#if defined(HAVE_LINUX) || defined(HAVE_OSX)
   char title[MAX_PROCESS_TITLE_LENGTH];
   size_t size;
   char** env = environ;
   int es = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   // sanity check: if the user does not want to
   // update the process title, do nothing
   if (config->update_process_title == UPDATE_PROCESS_TITLE_NEVER)
   {
      return;
   }

   if (!env_changed)
   {
      for (int i = 0; env[i] != NULL; i++)
      {
         es++;
      }

      environ = (char**)malloc(sizeof(char*) * (es + 1));
      if (environ == NULL)
      {
         return;
      }

      for (int i = 0; env[i] != NULL; i++)
      {
         size = strlen(env[i]);
         environ[i] = (char*)calloc(1, size + 1);

         if (environ[i] == NULL)
         {
            return;
         }
         memcpy(environ[i], env[i], size);
      }
      environ[es] = NULL;
      env_changed = true;
   }

   // compute how long was the command line
   // when the application was started
   if (max_process_title_size == 0)
   {
      for (int i = 0; i < argc; i++)
      {
         max_process_title_size += strlen(argv[i]) + 1;
      }
   }

   // compose the new title
   memset(&title, 0, sizeof(title));
   snprintf(title, sizeof(title) - 1, "pgagroal: %s%s%s",
            s1 != NULL ? s1 : "",
            s1 != NULL && s2 != NULL ? "/" : "",
            s2 != NULL ? s2 : "");

   // nuke the command line info
   memset(*argv, 0, max_process_title_size);

   // copy the new title over argv checking
   // the update_process_title policy
   if (config->update_process_title == UPDATE_PROCESS_TITLE_STRICT)
   {
      size = max_process_title_size;
   }
   else
   {
      // here we can set the title to a full description
      size = strlen(title) + 1;
   }

   memcpy(*argv, title, size);
   memset(*argv + size, 0, 1);

   // keep track of how long is now the title
   max_process_title_size = size;

#else
   setproctitle("-pgagroal: %s%s%s",
                s1 != NULL ? s1 : "",
                s1 != NULL && s2 != NULL ? "/" : "",
                s2 != NULL ? s2 : "");

#endif
}

void
pgagroal_set_connection_proc_title(int argc, char** argv, struct connection* connection)
{
   struct configuration* config;
   int primary;
   char* info = NULL;

   config = (struct configuration*)shmem;

   if (pgagroal_get_primary(&primary))
   {
      // cannot find the primary, this is a problem!
      pgagroal_set_proc_title(argc, argv, connection->username, connection->database);
      return;
   }

   info = pgagroal_append(info, connection->username);
   info = pgagroal_append(info, "@");
   info = pgagroal_append(info, config->servers[primary].host);
   info = pgagroal_append(info, ":");
   info = pgagroal_append_int(info, config->servers[primary].port);

   pgagroal_set_proc_title(argc, argv, info, connection->database);
   free(info);
}

unsigned int
pgagroal_version_as_number(unsigned int major, unsigned int minor, unsigned int patch)
{
   return (patch % 100)
          + (minor % 100) * 100
          + (major % 100) * 10000;
}

unsigned int
pgagroal_version_number(void)
{
   return pgagroal_version_as_number(PGAGROAL_MAJOR_VERSION,
                                     PGAGROAL_MINOR_VERSION,
                                     PGAGROAL_PATCH_VERSION);
}

bool
pgagroal_version_ge(unsigned int major, unsigned int minor, unsigned int patch)
{
   if (pgagroal_version_number() >= pgagroal_version_as_number(major, minor, patch))
   {
      return true;
   }
   else
   {
      return false;
   }
}

char*
pgagroal_append(char* orig, char* s)
{
   size_t orig_length;
   size_t s_length;
   char* n = NULL;

   if (s == NULL)
   {
      return orig;
   }

   if (orig != NULL)
   {
      orig_length = strlen(orig);
   }
   else
   {
      orig_length = 0;
   }

   s_length = strlen(s);

   n = (char*)realloc(orig, orig_length + s_length + 1);

   memcpy(n + orig_length, s, s_length);

   n[orig_length + s_length] = '\0';

   return n;
}

char*
pgagroal_append_int(char* orig, int i)
{
   char number[12];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 11, "%d", i);
   orig = pgagroal_append(orig, number);

   return orig;
}

char*
pgagroal_append_ulong(char* orig, unsigned long l)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%lu", l);
   orig = pgagroal_append(orig, number);

   return orig;
}

char*
pgagroal_append_ullong(char* orig, unsigned long long l)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%llu", l);
   orig = pgagroal_append(orig, number);

   return orig;
}

#ifdef DEBUG

int
pgagroal_backtrace(void)
{
#ifdef HAVE_LINUX
   void* array[100];
   size_t size;
   char** strings;

   size = backtrace(array, 100);
   strings = backtrace_symbols(array, size);

   for (size_t i = 0; i < size; i++)
   {
      printf("%s\n", strings[i]);
   }

   free(strings);
#endif

   return 0;
}

#endif

bool
parse_command(int argc,
              char** argv,
              int offset,
              char* command,
              char* subcommand,
              char** key,
              char* default_key,
              char** value,
              char* default_value)
{

   // sanity check: if no arguments, nothing to parse!
   if (argc <= offset)
   {
      return false;
   }

   // first of all check if the command is the same
   // as the first argument on the command line
   if (strncmp(argv[offset], command, MISC_LENGTH))
   {
      return false;
   }

   if (subcommand)
   {
      // thre must be a subcommand check
      offset++;

      if (argc <= offset)
      {
         // not enough command args!
         return false;
      }

      if (strncmp(argv[offset], subcommand, MISC_LENGTH))
      {
         return false;
      }
   }

   if (key)
   {
      // need to evaluate the database or server or configuration key
      offset++;
      *key = argc > offset ? argv[offset] : default_key;
      if (*key == NULL || strlen(*key) == 0)
      {
         goto error;
      }

      // do I need also a value?
      if (value)
      {
         offset++;
         *value = argc > offset ? argv[offset] : default_value;

         if (*value == NULL || strlen(*value) == 0)
         {
            goto error;
         }

      }
   }

   return true;

error:
   return false;
}

bool
parse_deprecated_command(int argc,
                         char** argv,
                         int offset,
                         char* command,
                         char** value,
                         char* deprecated_by,
                         unsigned int deprecated_since_major,
                         unsigned int deprecated_since_minor)
{
   // sanity check: if no arguments, nothing to parse!
   if (argc <= offset)
   {
      return false;
   }

   // first of all check if the command is the same
   // as the first argument on the command line
   if (strncmp(argv[offset], command, MISC_LENGTH))
   {
      return false;
   }

   if (value)
   {
      // need to evaluate the database or server
      offset++;
      *value = argc > offset ? argv[offset] : "*";
   }

   // warn the user if there is enough information
   // about deprecation
   if (deprecated_by
       && pgagroal_version_ge(deprecated_since_major, deprecated_since_minor, 0))
   {
      warnx("command <%s> has been deprecated by <%s> since version %d.%d",
            command, deprecated_by, deprecated_since_major, deprecated_since_minor);
   }

   return true;
}

bool
parse_command_simple(int argc,
                     char** argv,
                     int offset,
                     char* command,
                     char* subcommand)
{
   return parse_command(argc, argv, offset, command, subcommand, NULL, NULL, NULL, NULL);
}

/**
 * Given a server state, it returns a string that
 * described the state in a human-readable form.
 *
 * If the state cannot be determined, the numeric
 * form of the state is returned as a string.
 *
 * @param state the value of the sate for the server
 * @returns the string representing the state
 */
char*
pgagroal_server_state_as_string(signed char state)
{
   char* buf;

   switch (state)
   {
      case SERVER_NOTINIT:  return "Not init";
      case SERVER_NOTINIT_PRIMARY: return "Not init (primary)";
      case SERVER_PRIMARY: return "Primary";
      case SERVER_REPLICA: return "Replica";
      case SERVER_FAILOVER: return "Failover";
      case SERVER_FAILED: return "Failed";
      default:
         buf = malloc(5);
         memset(buf, 0, 5);
         snprintf(buf, 5, "%d", state);
         return buf;
   }
}
