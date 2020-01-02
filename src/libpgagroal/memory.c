/*
 * Copyright (C) 2020 Red Hat
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
#include <memory.h>

#define ZF_LOG_TAG "memory"
#include <zf_log.h>

/* system */
#include <stdlib.h>
#include <string.h>

static struct message* message = NULL;
static void* data = NULL;

/**
 *
 */
void
pgagroal_memory_init(void* shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!message)
      message = (struct message*)malloc(sizeof(struct message));

   if (!data)
      data = malloc((size_t)config->buffer_size);

   memset(message, 0, sizeof(struct message));
   memset(data, 0, (size_t)config->buffer_size);

   message->max_length = (size_t)config->buffer_size;
}

/**
 *
 */
struct message*
pgagroal_memory_message(void)
{
   return message;
}

/**
 *
 */
void*
pgagroal_memory_data(void)
{
   return data;
}

/**
 *
 */
void
pgagroal_memory_free(void)
{
   size_t length = message->max_length;

   memset(message, 0, sizeof(struct message));
   memset(data, 0, length);

   message->max_length = length;
}

/**
 *
 */
void
pgagroal_memory_destroy(void)
{
   free(data);
   free(message);
}
