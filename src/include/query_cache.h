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

/*

   variables:
    - query_cache: map<query, result>
    - query_cache_size: int
    - query_cache_max_size: int


   add init method: pgagroal_init_prometheus_cache
   invalidate method: metrics_cache_invalidate
   update method: metrics_cache_append
   get method: (impletent on ur own)

 */
#include <pgagroal.h>
#include <stdlib.h>

/**
 * Initialize the query cache with shared memory.
 *
 * This function initializes the query cache by creating a shared memory region
 * and initializing its structure. It also sets up a hash table for cache storage.
 *
 * @param p_size - Pointer to a size_t variable to store the allocated shared memory size.
 * @param p_shmem - Pointer to a void pointer to store the address of the allocated shared memory.
 *
 * @return 0 on success, 1 on failure.
 *
 * @note The function creates a shared memory region using 'pgagroal_create_shared_memory'.
 * @note The function sets the 'query_cache_max_size' in the configuration structure.
 * @note The function initializes a hash table within the query cache structure.
 *
 * @warning The caller should provide valid pointers to 'p_size' and 'p_shmem'.
 * @warning If the function returns 1, it indicates a failure, and 'query_cache_max_size' is set to 0.
 */
int
pgagroal_query_cache_init(size_t* p_size, void** p_shmem);

/**
 * Retrieve an entry from the query cache based on the given key.
 *
 * This function searches for an entry in the specified hash table using the provided key.
 *
 * @param Table - Pointer to a pointer to the hash table.
 * @param key - The key to search for in the hash table.
 *
 * @return Pointer to the found hash table entry associated with the provided key,
 *         or NULL if no entry is found for the given key.
 *
 * @note The function uses the HASH_FIND_PTR macro to efficiently search for the entry.
 * @note If no entry is found, the function returns NULL.
 *
 * @warning The caller should ensure the validity of the 'Table' pointer and the 'key' pointer.
 */
struct hashEntry*
pgagroal_query_cache_get(struct query_cache* cache, struct hashTable** Table, struct hashEntry* key);

/**
 * Invalidate a cache entry and remove it from the hash table.
 *
 * This function invalidates a cache entry by removing it from the specified hash table
 * based on the provided key. The memory associated with the removed entry is also freed.
 *
 * @param Table - Pointer to a pointer to the hash table.
 * @param key - The key identifying the entry to be invalidated and removed.
 *
 * @return 1 if the entry was successfully invalidated and removed, 0 if no entry was found for the given key.
 *
 * @note The function uses the HASH_FIND_PTR and HASH_DEL macros for efficient searching and removal.
 *
 * @warning The caller should ensure the validity of the 'Table' pointer and the 'key' pointer.
 * @warning The memory associated with the removed entry is freed within the function.
 */
int
pgagroal_query_cache_invalidate(struct hashTable** Table, struct hashEntry* key);

/**
 * Update the data associated with a cache entry in the hash table.
 *
 * This function updates the data associated with a cache entry in the specified hash table
 * based on the provided key. If the entry is found, its data is updated with the new data.
 *
 * @param Table - Pointer to a pointer to the hash table.
 * @param key - The key identifying the entry to be updated.
 * @param data - The new data to be associated with the cache entry.
 *
 * @return 1 if the entry was successfully found and updated, 0 if no entry was found for the given key.
 *
 * @note The function uses the HASH_FIND_PTR macro for efficient searching.
 *
 * @warning The caller should ensure the validity of the 'Table' pointer and the 'key' pointer.
 * @warning The function only updates the 'data' pointer and does not free any previously allocated memory.
 */
int
pgagroal_query_cache_update(struct hashTable** Table, struct hashEntry* key, struct hashEntry* data);

/**
 * Add a new cache entry to the hash table.
 *
 * This function adds a new cache entry to the specified hash table. The entry is created with
 * the provided data and key. If an entry with the same key already exists, the function returns 0
 * without making any changes. Otherwise, a new entry is allocated, populated, and added to the hash table.
 *
 * @param Table - Pointer to a pointer to the hash table.
 * @param data - The data to be associated with the new cache entry.
 * @param key - The key to identify the new cache entry.
 *
 * @return 1 if the entry was successfully added, 0 if an entry with the same key already exists.
 *
 * @note The function uses the HASH_FIND_PTR and HASH_ADD_PTR macros for efficient searching and adding.
 *
 * @warning The caller should ensure the validity of the 'Table' pointer and the 'key' pointer.
 * @warning The function allocates memory for the new entry using the 'malloc' function.
 *          The caller should manage memory to avoid leaks.
 */
int
pgagroal_query_cache_add(struct query_cache* cache, struct hashTable** Table, struct hashEntry* data, struct hashEntry* key, int flag);

/**
 * Clear all cache entries from the hash table and free associated memory.
 *
 * This function removes all cache entries from the specified hash table and
 * frees the associated memory for each entry. After calling this function, the
 * hash table will be empty.
 *
 * @param Table - Pointer to a pointer to the hash table to be cleared.
 *
 * @return 1 after successfully clearing all entries from the hash table.
 *
 * @note The function uses the HASH_ITER and HASH_DEL macros for iteration and removal.
 *
 * @warning The caller should ensure the validity of the 'Table' pointer.
 * @warning The function deallocates memory for each cleared entry using the 'free' function.
 */
int
pgagroal_query_cache_clear(struct hashTable** Table);

/**
 * Test the query cache functions by performing a series of cache operations.
 *
 * This function demonstrates the usage of various query cache functions:
 * - Adding cache entries
 * - Checking for the existence of a cache entry
 * - Updating cache entry data
 * - Invalidating cache entries
 * - Clearing all cache entries
 *
 * The function performs these operations using a sample cache structure.
 * It logs the results of each operation for demonstration purposes.
 */
void
pgagroal_query_cache_test(void);
