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
#include <utf8.h>

/* system */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
pgagroal_utf8_sequence_valid(const unsigned char* source, int length)
{
   unsigned char a;

   switch (length)
   {
      default:
         /* Only sequences of length 1-4 are valid */
         return false;

      case 4:
         /* Check 4th byte: must be 10xxxxxx */
         a = source[3];
         if (a < 0x80 || a > 0xBF)
         {
            return false;
         }
      /* FALL THRU */

      case 3:
         /* Check 3rd byte: must be 10xxxxxx */
         a = source[2];
         if (a < 0x80 || a > 0xBF)
         {
            return false;
         }
      /* FALL THRU */

      case 2:
         /* Check 2nd byte with special cases for specific start bytes */
         a = source[1];
         switch (source[0])
         {
            case 0xE0:
               /* 3-byte sequence starting with 0xE0 */
               /* Prevent overlong: must be 0xA0-0xBF to ensure codepoint >= 0x800 */
               if (a < 0xA0 || a > 0xBF)
               {
                  return false;
               }
               break;

            case 0xED:
               /* 3-byte sequence starting with 0xED */
               /* Prevent surrogates: must be 0x80-0x9F to ensure codepoint <= 0xD7FF */
               if (a < 0x80 || a > 0x9F)
               {
                  return false;
               }
               break;

            case 0xF0:
               /* 4-byte sequence starting with 0xF0 */
               /* Prevent overlong: must be 0x90-0xBF to ensure codepoint >= 0x10000 */
               if (a < 0x90 || a > 0xBF)
               {
                  return false;
               }
               break;

            case 0xF4:
               /* 4-byte sequence starting with 0xF4 */
               /* Prevent > U+10FFFF: must be 0x80-0x8F to ensure codepoint <= 0x10FFFF */
               if (a < 0x80 || a > 0x8F)
               {
                  return false;
               }
               break;

            default:
               /* For all other multi-byte sequences, 2nd byte must be 10xxxxxx */
               if (a < 0x80 || a > 0xBF)
               {
                  return false;
               }
               break;
         }
      /* FALL THRU */

      case 1:
         /* Check first byte */
         a = source[0];

         /* Invalid range: 0x80-0xC1 (continuation bytes and overlong 2-byte) */
         if (a >= 0x80 && a < 0xC2)
         {
            return false;
         }

         /* Invalid range: > 0xF4 (beyond Unicode range) */
         if (a > 0xF4)
         {
            return false;
         }
         break;
   }

   return true;
}

/**
 * Get the expected byte length of a UTF-8 sequence from its first byte.
 *
 * @param first_byte the first byte of the UTF-8 sequence
 * @return sequence length (1-4) or -1 if invalid start byte
 */
int
pgagroal_utf8_sequence_length(unsigned char first_byte)
{
   if (first_byte < 0x80)
   {
      return 1;  /* ASCII */
   }
   else if ((first_byte & 0xE0) == 0xC0)
   {
      return 2;  /* 110xxxxx */
   }
   else if ((first_byte & 0xF0) == 0xE0)
   {
      return 3;  /* 1110xxxx */
   }
   else if ((first_byte & 0xF8) == 0xF0)
   {
      return 4;  /* 11110xxx */
   }
   else
   {
      return -1; /* Invalid start byte */
   }
}

/**
 * Validates if the entire byte buffer contains valid UTF-8.
 *
 * @param buf pointer to the byte buffer
 * @param len length of the buffer in bytes
 * @return true if the entire string is valid UTF-8, false otherwise
 */
bool
pgagroal_utf8_valid(const unsigned char* buf, size_t len)
{
   size_t i = 0;

   while (i < len)
   {
      /* Get expected sequence length */
      int seq_length = pgagroal_utf8_sequence_length(buf[i]);

      if (seq_length < 0)
      {
         /* Invalid start byte */
         return false;
      }

      /* Check if we have enough bytes */
      if (i + seq_length > len)
      {
         /* Truncated sequence */
         return false;
      }

      /* Validate the sequence using strict UTF-8 rules */
      if (!pgagroal_utf8_sequence_valid(&buf[i], seq_length))
      {
         return false;
      }

      /* Move to next sequence */
      i += seq_length;
   }

   return true;
}

/**
 * Count the number of Unicode code points (characters) in a UTF-8 byte sequence.
 * Uses strict RFC 3629 validation for consistency.
 *
 * @param buf pointer to the byte buffer
 * @param len length of the buffer in bytes
 * @return number of Unicode characters, or (size_t)-1 if invalid UTF-8
 */
size_t
pgagroal_utf8_char_length(const unsigned char* buf, size_t len)
{
   size_t count = 0;
   size_t i = 0;

   while (i < len)
   {
      /* Get expected sequence length */
      int seq_length = pgagroal_utf8_sequence_length(buf[i]);

      if (seq_length < 0)
      {
         /* Invalid start byte */
         return (size_t)-1;
      }

      /* Check if we have enough bytes */
      if (i + seq_length > len)
      {
         /* Truncated sequence */
         return (size_t)-1;
      }

      /* Validate the sequence using strict UTF-8 rules */
      if (!pgagroal_utf8_sequence_valid(&buf[i], seq_length))
      {
         return (size_t)-1;
      }

      /* Move to next sequence and increment character count */
      i += seq_length;
      count++;
   }

   return count;
}

/**
 * Checks if a string contains only ASCII characters (0-127).
 *
 * @param str The input string.
 * @param len The length of the string in bytes.
 * @return true if all characters are ASCII, false otherwise.
 */
bool
pgagroal_is_ascii(const char* str, size_t len)
{
   for (size_t i = 0; i < len; i++)
   {
      if ((unsigned char)str[i] > 127)
      {
         return false;
      }
   }
   return true;
}