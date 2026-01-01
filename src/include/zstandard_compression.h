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

#ifndef PGAGROAL_ZSTANDARD_H
#define PGAGROAL_ZSTANDARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/**
 * ZSTD compress a string
 * @param s The original string
 * @param buffer The point to the compressed data buffer
 * @param buffer_size The size of the compressed buffer will be stored.
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_zstdc_string(char* s, unsigned char** buffer, size_t* buffer_size);

/**
 * ZSTD decompress a buffer to string
 * @param compressed_buffer The buffer containing the GZIP compressed data
 * @param compressed_size The size of the compressed buffer
 * @param output_string The pointer to a string where the decompressed data will be stored
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_zstdd_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string);

#ifdef __cplusplus
}
#endif

#endif
