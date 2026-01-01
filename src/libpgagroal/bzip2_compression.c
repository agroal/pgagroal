/*
 * Copyright (C) 2026 The pgagroal community
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
#include <bzip2_compression.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <bzlib.h>
#include <dirent.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LENGTH 8192

int
pgagroal_bzip2_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   size_t source_len;
   unsigned int dest_len;
   int bzip2_err;

   source_len = strlen(s);
   dest_len = source_len + (source_len * 0.01) + 600;

   *buffer = (unsigned char*)malloc(dest_len);
   if (!*buffer)
   {
      pgagroal_log_error("Bzip2: Allocation failed");
      return 1;
   }

   bzip2_err = BZ2_bzBuffToBuffCompress((char*)(*buffer), &dest_len, s, source_len, 9, 0, 0);
   if (bzip2_err != BZ_OK)
   {
      pgagroal_log_error("Bzip2: Compress failed");
      free(*buffer);
      return 1;
   }

   *buffer_size = dest_len;
   return 0;
}

int
pgagroal_bunzip2_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   int bzip2_err;
   unsigned int estimated_size = compressed_size * 10;
   unsigned int new_size;

   *output_string = (char*)malloc(estimated_size);
   if (!*output_string)
   {
      pgagroal_log_error("Bzip2: Allocation failed");
      return 1;
   }

   bzip2_err = BZ2_bzBuffToBuffDecompress(*output_string, &estimated_size, (char*)compressed_buffer, compressed_size, 0, 0);

   if (bzip2_err == BZ_OUTBUFF_FULL)
   {
      new_size = estimated_size * 2;
      char* temp = realloc(*output_string, new_size);

      if (!temp)
      {
         pgagroal_log_error("Bzip2: Reallocation failed");
         free(*output_string);
         return 1;
      }

      *output_string = temp;

      bzip2_err = BZ2_bzBuffToBuffDecompress(*output_string, &new_size, (char*)compressed_buffer, compressed_size, 0, 0);
      if (bzip2_err != BZ_OK)
      {
         pgagroal_log_error("Bzip2: Decompress failed");
         free(*output_string);
         return 1;
      }
      estimated_size = new_size;
   }
   else if (bzip2_err != BZ_OK)
   {
      pgagroal_log_error("Bzip2: Decompress failed");
      free(*output_string);
      return 1;
   }

   return 0;
}
