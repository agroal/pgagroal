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
#include <shmem.h>

/* system */
#include <stdlib.h>
#include <sys/mman.h>

void*
pgagroal_create_shared_memory(size_t size)
{
   int protection = PROT_READ | PROT_WRITE;
   int visibility = MAP_ANONYMOUS | MAP_SHARED;

   return mmap(NULL, size, protection, visibility, 0, 0);
}

void
pgagroal_resize_shared_memory(size_t size, void* shmem, size_t* new_size, void** new_shmem)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   *new_size = size + (config->max_connections * sizeof(struct connection));
   *new_shmem = pgagroal_create_shared_memory(*new_size);

   memset(*new_shmem, 0, *new_size);
   memcpy(*new_shmem, shmem, size);
}

int
pgagroal_destroy_shared_memory(void* shmem, size_t size)
{
   return munmap(shmem, size);
}
