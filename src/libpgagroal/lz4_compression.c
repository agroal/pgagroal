/*
 * Copyright (C) 2021 Red Hat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* pgagroal */
#include <pgagroal.h>
#include <logging.h>
#include <lz4_compression.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include "lz4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int
pgagroal_lz4c_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   size_t input_size;
   size_t max_compressed_size;
   int compressed_size;

   input_size = strlen(s);
   max_compressed_size = LZ4_compressBound(input_size);

   *buffer = (unsigned char*)malloc(max_compressed_size);
   if (*buffer == NULL)
   {
      pgagroal_log_error("LZ4: Allocation failed");
      return 1;
   }

   compressed_size = LZ4_compress_default(s, (char*)*buffer, input_size, max_compressed_size);
   if (compressed_size <= 0)
   {
      pgagroal_log_error("LZ4: Compress failed");
      free(*buffer);
      return 1;
   }

   *buffer_size = (size_t)compressed_size;

   return 0;
}

int
pgagroal_lz4d_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   size_t max_decompressed_size;
   int decompressed_size;

   max_decompressed_size = compressed_size * 4;

   *output_string = (char*)malloc(max_decompressed_size);
   if (*output_string == NULL)
   {
      pgagroal_log_error("LZ4: Allocation failed");
      return 1;
   }

   decompressed_size = LZ4_decompress_safe((const char*)compressed_buffer, *output_string, compressed_size, max_decompressed_size);
   if (decompressed_size < 0)
   {
      pgagroal_log_error("LZ4: Decompress failed");
      free(*output_string);
      return 1;
   }

   (*output_string)[decompressed_size] = '\0';

   return 0;
}
