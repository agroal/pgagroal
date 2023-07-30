/*
 * Copyright (C) 2023 Red Hat
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

#include <pgagroal.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <query_cache.h>
#include <utils.h>
#include <shmem.h>
#include <uthash.h>
#include <stdatomic.h>

/* system */
#include <ev.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define QUERY_CACHE_MAX_SIZE 1024 * 1024 * 256
#define QUERY_CACHE_MAX_ENTRIES 100000
#define HASH_ENTRY_DATA_SIZE 1024 * 1024 //1MB
#define HASH_ENTRY_KEY_SIZE 1024 //1KB

int
pgagroal_query_cache_init(size_t* p_size, void** p_shmem)
{
   struct query_cache* cache = NULL;
   struct configuration* config;

   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   // first of all, allocate the overall cache structure
   config->query_cache_max_size = QUERY_CACHE_MAX_SIZE;
   cache_size = config->query_cache_max_size;

   pgagroal_log_info("Query cache initialised");

   cache = (struct query_cache*)malloc(cache_size);
   if (cache == NULL)
   {
      goto error;
   }

   memset(cache, 0, cache_size);
   cache->size = cache_size;
   // initalizing the hash table

   atomic_init(&cache->lock, STATE_FREE);
   cache->max_elements = 0;
   // success! do the memory swap
   *p_shmem = cache;
   *p_size = cache_size;
   return 0;

error:
   // disable query caching
   config->query_cache_max_size = 0;
   pgagroal_log_error("Cannot allocate shared memory for the Query cache!");
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}
struct hashEntry*
pgagroal_query_cache_get(struct query_cache* cache, struct hashTable** Table, struct hashEntry* key)
{
   for (int i = 0; i < cache->max_elements; i++)
   {

      int x = strncmp(cache->cache[i].key->key, key->key, key->length);

      if (x == 0)
      {

         return cache->cache[i].data;
      }
   }
   return NULL;

}

int
pgagroal_query_cache_invalidate(struct hashTable** Table, struct hashEntry* key)
{
   struct hashTable* s;
   HASH_FIND(hh, *Table, key->value, key->length, s);
   if (s == NULL)
   {
      return 0;
   }
   HASH_DEL(*Table, s);
   free(s);
   return 1;
}

int
pgagroal_query_cache_update(struct hashTable** Table, struct hashEntry* key, struct hashEntry* data)
{

   struct hashTable* s = NULL;
   void* qkey = malloc(strlen(key->value) + 1);
   strcpy(qkey, key->value);
   HASH_FIND(hh, *Table, qkey, strlen(qkey), s);
   HASH_DEL(*Table, s);
   if (s == NULL)
   {
      return 0;
   }
   if (data->length > HASH_ENTRY_DATA_SIZE)
   {
      free(s);
      return 0;
   }

   s->data = data;
   HASH_ADD_KEYPTR(hh, *Table, s->key->value, strlen(s->key->value), s);
   return 1;
}

int
pgagroal_query_cache_add(struct query_cache* cache, struct hashTable** Table, struct hashEntry* data, struct hashEntry* key, int flag)
{
   if (cache->max_elements >= QUERY_CACHE_MAX_ENTRIES)
   {
      pgagroal_log_warn("Cache is full %d", cache->max_elements);

      return -1;
   }

   int idx = cache->max_elements;

   cache->cache[idx].key = key;
   cache->cache[idx].data = data;
   cache->max_elements = idx + 1;

   return 1;

}

int
pgagroal_query_cache_clear(struct hashTable** Table)
{
   struct hashTable* current_item, * tmp;
   HASH_ITER(hh, *Table, current_item, tmp)
   {
      HASH_DEL(*Table, current_item);
      free(current_item);
   }
   return 1;
}
void
pgagroal_query_cache_test(void)
{
   return;
}
