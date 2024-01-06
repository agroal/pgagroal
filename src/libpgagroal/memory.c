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
#include <memory.h>
#include <message.h>
#ifdef DEBUG
#include <utils.h>
#endif

/* system */
#ifdef DEBUG
#include <assert.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct message* message = NULL;
static void* data = NULL;

/**
 *
 */
void
pgagroal_memory_init(void)
{
   struct configuration* config;

#ifdef DEBUG
   assert(shmem != NULL);
#endif

   config = (struct configuration*)shmem;

   pgagroal_memory_size(config->buffer_size);
}

/**
 *
 */
void
pgagroal_memory_size(size_t size)
{
   pgagroal_memory_destroy();

   message = (struct message*)malloc(sizeof(struct message));
   if (message == NULL)
   {
      goto error;
   }

   data = malloc(size);
   if (data == NULL)
   {
      goto error;
   }

   memset(message, 0, sizeof(struct message));
   memset(data, 0, size);

   message->kind = 0;
   message->length = 0;
   message->max_length = size;
   message->data = data;

   return;

error:

   pgagroal_log_fatal("Unable to allocate memory");

#ifdef DEBUG
   pgagroal_backtrace();
#endif

   errno = 0;
}

/**
 *
 */
struct message*
pgagroal_memory_message(void)
{
#ifdef DEBUG
   assert(message != NULL);
   assert(data != NULL);
#endif

   return message;
}

/**
 *
 */
void
pgagroal_memory_free(void)
{
   size_t length;

#ifdef DEBUG
   assert(message != NULL);
   assert(data != NULL);
#endif

   length = message->max_length;

   memset(message, 0, sizeof(struct message));
   memset(data, 0, length);

   message->kind = 0;
   message->length = 0;
   message->max_length = length;
   message->data = data;
}

/**
 *
 */
void
pgagroal_memory_destroy(void)
{
   free(data);
   free(message);

   data = NULL;
   message = NULL;
}
