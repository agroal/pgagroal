/*
 * Copyright (C) 2025 The pgagroal community
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
#include <zstandard_compression.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ZSTD_DEFAULT_NUMBER_OF_WORKERS 4

int
pgagroal_zstdc_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   size_t input_size;
   size_t max_compressed_size;
   size_t compressed_size;

   input_size = strlen(s);
   max_compressed_size = ZSTD_compressBound(input_size);

   *buffer = (unsigned char*)malloc(max_compressed_size);
   if (*buffer == NULL)
   {
      pgagroal_log_error("ZSTD: Allocation failed");
      return 1;
   }

   compressed_size = ZSTD_compress(*buffer, max_compressed_size, s, input_size, 1);
   if (ZSTD_isError(compressed_size))
   {
      pgagroal_log_error("ZSTD: Compression error: %s", ZSTD_getErrorName(compressed_size));
      free(*buffer);
      return 1;
   }

   *buffer_size = compressed_size;

   return 0;
}

int
pgagroal_zstdd_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   size_t decompressed_size;
   size_t result;

   decompressed_size = ZSTD_getFrameContentSize(compressed_buffer, compressed_size);

   if (decompressed_size == ZSTD_CONTENTSIZE_ERROR)
   {
      pgagroal_log_error("ZSTD: Not a valid compressed buffer");
      return 1;
   }
   if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
   {
      pgagroal_log_error("ZSTD: Unknown decompressed size");
      return 1;
   }

   *output_string = (char*)malloc(decompressed_size + 1);
   if (*output_string == NULL)
   {
      pgagroal_log_error("ZSTD: Allocation failed");
      return 1;
   }

   result = ZSTD_decompress(*output_string, decompressed_size, compressed_buffer, compressed_size);
   if (ZSTD_isError(result))
   {
      pgagroal_log_error("ZSTD: Compression error: %s", ZSTD_getErrorName(result));
      free(*output_string);
      return 1;
   }

   (*output_string)[decompressed_size] = '\0';

   return 0;
}
