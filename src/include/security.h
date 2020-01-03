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

#ifndef PGAGROAL_SECURITY_H
#define PGAGROAL_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdlib.h>

/**
 * Authenticate a user
 * @param client_fd The descriptor
 * @param address The client address
 * @param shmem The shared memory segment
 * @param slot The resulting slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_authenticate(int client_fd, char* address, void* shmem, int* slot);

/**
 * Authenticate a prefill connection
 * @param username The user name
 * @param password The password
 * @param database The database
 * @param shmem The shared memory segment
 * @param slot The resulting slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_prefill_auth(char* username, char* password, char* database, void* shmem, int* slot);

/**
 * Get the master key
 * @param masterkey The master key
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_get_master_key(char** masterkey);

/**
 * Encrypt a string
 * @param plaintext The string
 * @param password The master password
 * @param ciphertext The ciphertext output
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_encrypt(char* plaintext, char* password, char** ciphertext);

/**
 * Decrypt a string
 * @param ciphertext The string
 * @param password The master password
 * @param plaintext The plaintext output
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_decrypt(char* ciphertext, char* password, char** plaintext);

/**
 * MD5 a string
 * @param str The string
 * @param md5 The MD5 string
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_md5(char* str, int length, char** md5);

#ifdef __cplusplus
}
#endif

#endif
