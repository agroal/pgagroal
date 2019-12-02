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
#include <utils.h>

#define ZF_LOG_TAG "utils"
#include <zf_log.h>

/* system */
#include <ev.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/pem.h>
#include <sys/types.h>

#ifndef EVBACKEND_LINUXAIO
#define EVBACKEND_LINUXAIO 0x00000040U
#endif

#ifndef EVBACKEND_IOURING
#define EVBACKEND_IOURING  0x00000080U
#endif

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
                          ((bytes[1]     ));

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
                          ((bytes[2] <<  8)) |
                          ((bytes[3]      ));

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
                    (((long)bytes[6]) <<  8) |
                    (((long)bytes[7])      );

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
   char *ptr = (char*)&i;

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
   char *ptr = (char*)&l;

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

void
pgagroal_libev_engines()
{
   unsigned int engines = ev_supported_backends();

   if (engines & EVBACKEND_SELECT)
   {
      ZF_LOGD("libev available: select");
   }
   if (engines & EVBACKEND_POLL)
   {
      ZF_LOGD("libev available: poll");
   }
   if (engines & EVBACKEND_EPOLL)
   {
      ZF_LOGD("libev available: epoll");
   }
   if (engines & EVBACKEND_LINUXAIO)
   {
      ZF_LOGD("libev available: linuxaio");
   }
   if (engines & EVBACKEND_IOURING)
   {
      ZF_LOGD("libev available: iouring");
   }
   if (engines & EVBACKEND_KQUEUE)
   {
      ZF_LOGD("libev available: kqueue");
   }
   if (engines & EVBACKEND_DEVPOLL)
   {
      ZF_LOGD("libev available: devpoll");
   }
   if (engines & EVBACKEND_PORT)
   {
      ZF_LOGD("libev available: port");
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
            ZF_LOGW("libev not available: select");
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
            ZF_LOGW("libev not available: poll");
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
            ZF_LOGW("libev not available: epoll");
         }
      }
      else if (!strcmp("linuxaio", engine))
      {
         if (engines & EVBACKEND_LINUXAIO)
         {
            return EVBACKEND_LINUXAIO;
         }
         else
         {
            ZF_LOGW("libev not available: linuxaio");
         }
      }
      else if (!strcmp("iouring", engine))
      {
         if (engines & EVBACKEND_IOURING)
         {
            return EVBACKEND_IOURING;
         }
         else
         {
            ZF_LOGW("libev not available: iouring");
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
            ZF_LOGW("libev not available: devpoll");
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
            ZF_LOGW("libev not available: port");
         }
      }
      else if (!strcmp("auto", engine) || !strcmp("", engine))
      {
         return EVFLAG_AUTO;
      }
      else
      {
         ZF_LOGW("libev unknown option: %s", engine);
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
pgagroal_get_home_directory()
{
   struct passwd *pw = getpwuid(getuid());

   return pw->pw_dir;
}

int
pgagroal_base64_encode(char* raw, char** encoded)
{
   BIO* b64_bio;
   BIO* mem_bio;
   BUF_MEM* mem_bio_mem_ptr;

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);
   BIO_write(b64_bio, raw, strlen(raw));
   BIO_flush(b64_bio);

   BIO_get_mem_ptr(mem_bio, &mem_bio_mem_ptr);

   BIO_set_close(mem_bio, BIO_NOCLOSE);
   BIO_free_all(b64_bio);

   BUF_MEM_grow(mem_bio_mem_ptr, (*mem_bio_mem_ptr).length + 1);
   (*mem_bio_mem_ptr).data[(*mem_bio_mem_ptr).length] = '\0';

   *encoded = (*mem_bio_mem_ptr).data;

   return 0;
}

int
pgagroal_base64_decode(char* encoded, char** raw)
{
   BIO* b64_bio;
   BIO* mem_bio;
   int length;
   size_t size;
   char* decoded;
   int index;

   length = strlen(encoded);
   size = (length * 3) / 4 + 1;
   decoded = malloc(size);
   memset(decoded, 0, size);

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_write(mem_bio, encoded, length);
   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

   index = 0;
   while (0 < BIO_read(b64_bio, decoded + index, 1) )
   {
      index++;
   }

   BIO_free_all(b64_bio);

   *raw = decoded;

   return 0;
}

#ifdef DEBUG

static void decode_frontend_zero(struct message* msg);
static void decode_frontend_Q(struct message* msg);
static void decode_frontend_p(struct message* msg);

static void decode_backend_C(struct message* msg, int offset);
static void decode_backend_D(struct message* msg, int offset);
static void decode_backend_E(struct message* msg);
static void decode_backend_K(struct message* msg, int offset);
static void decode_backend_R(struct message* msg, int offset);
static void decode_backend_S(struct message* msg, int offset);
static void decode_backend_T(struct message* msg);
static void decode_backend_Z(struct message* msg, int offset);

/**
 *
 */
void
pgagroal_decode_message(struct message* msg)
{
   ZF_LOGV_MEM(msg->data, msg->length, "Message %p:", (const void *)msg->data);

   switch (msg->kind)
   {
      case 0:
         decode_frontend_zero(msg);
         break;
      case 'Q':
         decode_frontend_Q(msg);
         break;
      case 'p':
         decode_frontend_p(msg);
         break;
      case 'C':
         decode_backend_C(msg, 0);
         break;
      case 'D':
         decode_backend_D(msg, 0);
         break;
      case 'E':
         decode_backend_E(msg);
         break;
      case 'K':
         decode_backend_K(msg, 0);
         break;
      case 'R':
         decode_backend_R(msg, 0);
         break;
      case 'S':
         decode_backend_S(msg, 0);
         break;
      case 'T':
         decode_backend_T(msg);
         break;
      case 'Z':
         decode_backend_Z(msg, 0);
         break;
      default:
         break;
   }
}

static void
decode_frontend_zero(struct message* msg)
{
   int start, end;
   int counter;
   signed char c;
   char** array = NULL;
   int32_t length;
   int32_t request;

   if (msg->length < 8)
      return;

   length = pgagroal_read_int32(msg->data);
   request = pgagroal_read_int32(msg->data + 4);
   
   ZF_LOGV("Frontend: 0 Length: %d Request: %d", length, request);

   if (request == 196608)
   {
      counter = 0;

      /* We know where the parameters start, and we know that the message is zero terminated */
      for (int i = 8; i < msg->length - 1; i++)
      {
         c = pgagroal_read_byte(msg->data + i);
         if (c == 0)
            counter++;
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
            array[counter] = (char*)malloc(end - start);
            memset(array[counter], 0, end - start);
            memcpy(array[counter], msg->data + start, end - start);
               
            start = end;
            counter++;
         }
      }
         
      for (int i = 0; i < counter; i++)
         ZF_LOGV("Frontend: 0/Req Data: %s", array[i]);

      for (int i = 0; i < counter; i++)
         free(array[i]);
      free(array);
   }
   else if (request == 80877103)
   {
      /* SSL: Not supported */
   }
   else if (request == 80877104)
   {
      /* GSS: Not supported */
   }
   else
   {
      printf("Unknown request: %d\n", request);
      exit(1);
   }
}

static void
decode_frontend_Q(struct message* msg)
{
   ZF_LOGV("Frontend: Q");
   /* ZF_LOGV("Data: %s", pgagroal_read_string(msg->data + 5)); */
}


static void
decode_frontend_p(struct message* msg)
{
   ZF_LOGV("Frontend: p");
   ZF_LOGV("Data: %s", pgagroal_read_string(msg->data + 5));
}


static void
decode_backend_C(struct message* msg, int offset)
{
   char* str = NULL;

   str = pgagroal_read_string(msg->data + offset + 5);
   offset += 5;
   
   ZF_LOGV("Backend: C");
   ZF_LOGV("Data: %s", str);

   offset += strlen(str) + 1;

   if (offset < msg->length)
   {
      signed char peek = pgagroal_read_byte(msg->data + offset);
      switch (peek)
      {
          case 'Z':
             decode_backend_Z(msg, offset);
             break;
          default:
             ZF_LOGV("C: Peek %d", peek);
             break;
      }
   }
}

static void
decode_backend_D(struct message* msg, int offset)
{
   int16_t number_of_columns;
   int32_t column_length;

   number_of_columns = pgagroal_read_int16(msg->data + offset + 5);
   offset += 7;

   ZF_LOGV("Backend: D");
   ZF_LOGV("Number: %d", number_of_columns);
   for (int16_t i = 0; i < number_of_columns; i++)
   {
      column_length = pgagroal_read_int32(msg->data + offset);
      offset += 4;

      char buf[column_length + 1];
      memset(&buf, 0, column_length + 1);
      
      for (int16_t j = 0; j < column_length; j++)
      {
         buf[j] = pgagroal_read_byte(msg->data + offset);
         offset += 1;
      }

      ZF_LOGV("Length: %d", column_length);
      ZF_LOGV("Data  : %s", buf);
   }

   if (offset < msg->length)
   {
      signed char peek = pgagroal_read_byte(msg->data + offset);
      switch (peek)
      {
          case 'C':
             decode_backend_C(msg, offset);
             break;
          case 'D':
             decode_backend_D(msg, offset);
             break;
          default:
             ZF_LOGV("D: Peek %d", peek);
             break;
      }
   }
}

static void
decode_backend_E(struct message* msg)
{
   int32_t length;
   int offset;
   signed char type;
   char* str;

   length = pgagroal_read_int32(msg->data + 1);
   offset = 5;

   ZF_LOGV("Backend: E");
   while (offset < length - 4)
   {
      type = pgagroal_read_byte(msg->data + offset);
      str = pgagroal_read_string(msg->data + offset + 1);

      ZF_LOGV("Data: %c %s", type, str);

      offset += (strlen(str) + 2);
   }
}

static void
decode_backend_K(struct message* msg, int offset)
{
   int32_t process;
   int32_t secret;

   offset += 5;

   process = pgagroal_read_int32(msg->data + offset);
   offset += 4;

   secret = pgagroal_read_int32(msg->data + offset);
   offset += 4;

   ZF_LOGV("Backend: K");
   ZF_LOGV("Process: %d", process);
   ZF_LOGV("Secret : %d", secret);

   if (offset < msg->length)
   {
      signed char peek = pgagroal_read_byte(msg->data + offset);
      switch (peek)
      {
          case 'Z':
             decode_backend_Z(msg, offset);
             break;
          default:
             ZF_LOGV("K: Peek %d", peek);
             break;
      }
   }
}

static void
decode_backend_R(struct message* msg, int offset)
{
   int32_t length;
   int32_t type;

   length = pgagroal_read_int32(msg->data + offset + 1);
   type = pgagroal_read_int32(msg->data + offset + 5);
   offset += 9;

   switch (type)
   {
      case 0:
         ZF_LOGV("Backend: R - Success");
         break;
      case 2:
         ZF_LOGV("Backend: R - KerberosV5");
         break;
      case 3:
         ZF_LOGV("Backend: R - CleartextPassword");
         break;
      case 5:
         ZF_LOGV("Backend: R - MD5Password");
         ZF_LOGV("             Salt %02hhx%02hhx%02hhx%02hhx",
                 (signed char)(pgagroal_read_byte(msg->data + 9) & 0xFF),
                 (signed char)(pgagroal_read_byte(msg->data + 10) & 0xFF),
                 (signed char)(pgagroal_read_byte(msg->data + 11) & 0xFF),
                 (signed char)(pgagroal_read_byte(msg->data + 12) & 0xFF));
         offset += 4;
         break;
      case 6:
         ZF_LOGV("Backend: R - SCMCredential");
         break;
      case 7:
         ZF_LOGV("Backend: R - GSS");
         break;
      case 8:
         ZF_LOGV("Backend: R - GSSContinue");
         break;
      case 9:
         ZF_LOGV("Backend: R - SSPI");
         break;
      case 10:
         ZF_LOGV("Backend: R - SASL");
         while (offset < length - 8)
         {
            char* mechanism = pgagroal_read_string(msg->data + offset);
            ZF_LOGV("             %s", mechanism);
            offset += strlen(mechanism) + 1;
         }
         break;
      case 11:
         ZF_LOGV("Backend: R - SASLContinue");
         ZF_LOGV_MEM(msg->data + offset, length - 8, "Message %p:", (const void *)msg->data + offset);
         offset += length - 8;
         break;
      case 12:
         ZF_LOGV("Backend: R - SASLFinal");
         ZF_LOGV_MEM(msg->data + offset, length - 8, "Message %p:", (const void *)msg->data + offset);
         offset += length - 8;
         break;
      default:
         break;
   }

   if (offset < msg->length)
   {
      signed char peek = pgagroal_read_byte(msg->data + offset);
      switch (peek)
      {
          case 'R':
             decode_backend_R(msg, offset);
             break;
          case 'S':
             decode_backend_S(msg, offset);
             break;
          default:
             ZF_LOGV("R: Peek %d", peek);
             break;
      }
   }
}

static void
decode_backend_S(struct message* msg, int offset)
{
   char* name = NULL;
   char* value = NULL;

   offset += 5;

   name = pgagroal_read_string(msg->data + offset);
   offset += strlen(name) + 1;

   value = pgagroal_read_string(msg->data + offset);
   offset += strlen(value) + 1;

   ZF_LOGV("Backend: S");
   ZF_LOGV("Name : %s", name);
   ZF_LOGV("Value: %s", value);

   if (offset < msg->length)
   {
      signed char peek = pgagroal_read_byte(msg->data + offset);
      switch (peek)
      {
          case 'S':
             decode_backend_S(msg, offset);
             break;
          case 'K':
             decode_backend_K(msg, offset);
             break;
          default:
             ZF_LOGV("S: Peek %d", peek);
             break;
      }
   }
}

static void
decode_backend_T(struct message* msg)
{
   int16_t number_of_fields;
   char* field_name = NULL;
   int32_t oid;
   int16_t attr;
   int32_t type_oid;
   int16_t type_length;
   int32_t type_modifier;
   int16_t format;
   int offset;

   number_of_fields = pgagroal_read_int16(msg->data + 5);
   offset = 7;

   ZF_LOGV("Backend: T");
   ZF_LOGV("Number       : %d", number_of_fields);
   for (int16_t i = 0; i < number_of_fields; i++)
   {
      field_name = pgagroal_read_string(msg->data + offset);
      offset += strlen(field_name) + 1;

      oid = pgagroal_read_int32(msg->data + offset);
      offset += 4;

      attr = pgagroal_read_int16(msg->data + offset);
      offset += 2;

      type_oid = pgagroal_read_int32(msg->data + offset);
      offset += 4;

      type_length = pgagroal_read_int16(msg->data + offset);
      offset += 2;

      type_modifier = pgagroal_read_int32(msg->data + offset);
      offset += 4;

      format = pgagroal_read_int16(msg->data + offset);
      offset += 2;

      ZF_LOGV("Name         : %s", field_name);
      ZF_LOGV("OID          : %d", oid);
      ZF_LOGV("Attribute    : %d", attr);
      ZF_LOGV("Type OID     : %d", type_oid);
      ZF_LOGV("Type length  : %d", type_length);
      ZF_LOGV("Type modifier: %d", type_modifier);
      ZF_LOGV("Format       : %d", format);
   }

   if (offset < msg->length)
   {
      signed char peek = pgagroal_read_byte(msg->data + offset);
      switch (peek)
      {
          case 'C':
             decode_backend_C(msg, offset);
             break;
          case 'D':
             decode_backend_D(msg, offset);
             break;
          default:
             ZF_LOGV("T: Peek %d", peek);
             break;
      }
   }
}

static void
decode_backend_Z(struct message* msg, int offset)
{
   char buf[2];

   memset(&buf, 0, 2);

   buf[0] = pgagroal_read_byte(msg->data + offset + 5);
   offset += 5;
   
   ZF_LOGV("Backend: Z");
   ZF_LOGV("Data: %s", buf);
}


#endif
