/*-------------------------------------------------------------------------
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

#ifndef PGAGROAL_UTF8_H
#define PGAGROAL_UTF8_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Counts the number of Unicode code points in a UTF-8 byte sequence.
 * @param buf The UTF-8 byte buffer.
 * @param len The length of the buffer in bytes.
 * @return The number of code points, or -1 on error.
 */
size_t
pgagroal_utf8_char_length(const unsigned char* buf, size_t len);

/**
 * Checks if a string contains only ASCII characters (0-127).
 * @param str The input string.
 * @param len The length of the string in bytes.
 * @return true if all characters are ASCII, false otherwise.
 */
bool
pgagroal_is_ascii(const char* str, size_t len);

/**
 * Validates if the entire byte buffer contains valid UTF-8.
 * @param buf The UTF-8 byte buffer.
 * @param len The length of the buffer in bytes.
 * @return true if valid UTF-8, false otherwise.
 */
bool
pgagroal_utf8_valid(const unsigned char* buf, size_t len);

/**
 * Validates a single UTF-8 sequence (1-4 bytes) according to RFC 3629.
 * Checks for overlong encodings, surrogate pairs, and Unicode range limits.
 *
 * Derived from pg_utf8_islegal in PostgreSQL:
 * https://www.postgresql.org/about/licence/
 * Licensed under the PostgreSQL License.
 *
 * @param source pointer to the UTF-8 sequence
 * @param length expected length of the sequence (1-4)
 * @return true if the sequence is valid, false otherwise
 */
bool
pgagroal_utf8_sequence_valid(const unsigned char* source, int length);

/**
 * Get the expected byte length of a UTF-8 sequence from its first byte.
 * @param first_byte the first byte of the UTF-8 sequence
 * @return sequence length (1-4) or -1 if invalid start byte
 */
int
pgagroal_utf8_sequence_length(unsigned char first_byte);

#endif /* UTF8_H */