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
#include <gzip_compression.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LENGTH 8192

int
pgagroal_gzip_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   int ret;
   z_stream stream;
   size_t source_len;
   size_t chunk_size;
   unsigned char* temp_buffer;
   unsigned char* final_buffer;

   source_len = strlen(s);
   chunk_size = BUFFER_LENGTH;

   temp_buffer = (unsigned char*)malloc(chunk_size);
   if (temp_buffer == NULL)
   {
      pgagroal_log_error("Gzip: Allocation error");
      return 1;
   }

   memset(&stream, 0, sizeof(stream));
   stream.next_in = (unsigned char*)s;
   stream.avail_in = source_len;

   ret = deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
   if (ret != Z_OK)
   {
      free(temp_buffer);
      pgagroal_log_error("Gzip: Initialization failed");
      return 1;
   }

   size_t total_out = 0;
   do
   {
      if (stream.total_out >= chunk_size)
      {
         chunk_size *= 2;
         unsigned char* new_buffer = (unsigned char*)realloc(temp_buffer, chunk_size);
         if (new_buffer == NULL)
         {
            free(temp_buffer);
            deflateEnd(&stream);
            pgagroal_log_error("Gzip: Allocation error");
            return 1;
         }
         temp_buffer = new_buffer;
      }

      stream.next_out = temp_buffer + stream.total_out;
      stream.avail_out = chunk_size - stream.total_out;

      ret = deflate(&stream, Z_FINISH);
   }
   while (ret == Z_OK || ret == Z_BUF_ERROR);

   if (ret != Z_STREAM_END)
   {
      free(temp_buffer);
      deflateEnd(&stream);
      pgagroal_log_error("Gzip: Compression failed");
      return 1;
   }

   total_out = stream.total_out;

   final_buffer = (unsigned char*)realloc(temp_buffer, total_out);
   if (final_buffer == NULL)
   {
      *buffer = temp_buffer;
   }
   else
   {
      *buffer = final_buffer;
   }
   *buffer_size = total_out;

   deflateEnd(&stream);

   return 0;
}

int
pgagroal_gunzip_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   int ret;
   z_stream stream;
   size_t chunk_size;
   size_t total_out = 0;

   chunk_size = BUFFER_LENGTH;

   char* temp_buffer = (char*)malloc(chunk_size);
   if (temp_buffer == NULL)
   {
      pgagroal_log_error("GUNzip: Allocation failed");
      return 1;
   }

   memset(&stream, 0, sizeof(stream));
   stream.next_in = (unsigned char*)compressed_buffer;
   stream.avail_in = compressed_size;

   ret = inflateInit2(&stream, MAX_WBITS + 16);
   if (ret != Z_OK)
   {
      free(temp_buffer);
      pgagroal_log_error("GUNzip: Initialization failed");
      return 1;
   }

   do
   {
      if (stream.total_out >= chunk_size)
      {
         chunk_size *= 2;
         char* new_buffer = (char*)realloc(temp_buffer, chunk_size);
         if (new_buffer == NULL)
         {
            free(temp_buffer);
            inflateEnd(&stream);
            pgagroal_log_error("GUNzip: Allocation error");
            return 1;
         }
         temp_buffer = new_buffer;
      }

      stream.next_out = (unsigned char*)(temp_buffer + stream.total_out);
      stream.avail_out = chunk_size - stream.total_out;

      ret = inflate(&stream, Z_NO_FLUSH);
   }
   while (ret == Z_OK || ret == Z_BUF_ERROR);

   if (ret != Z_STREAM_END)
   {
      free(temp_buffer);
      inflateEnd(&stream);
      pgagroal_log_error("GUNzip: Decompression failed");
      return 1;
   }

   total_out = stream.total_out;

   char* final_buffer = (char*)realloc(temp_buffer, total_out + 1);
   if (final_buffer == NULL)
   {
      free(temp_buffer);
      inflateEnd(&stream);
      pgagroal_log_error("GUNzip: Allocation failed");
      return 1;
   }
   temp_buffer = final_buffer;

   temp_buffer[total_out] = '\0';

   *output_string = temp_buffer;

   inflateEnd(&stream);

   return 0;
}
