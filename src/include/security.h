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

#include <openssl/ssl.h>

/**
 * Authenticate a user
 * @param client_fd The descriptor
 * @param address The client address
 * @param slot The resulting slot
 * @param client_ssl The client SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_authenticate(int client_fd, char* address, int* slot, SSL** client_ssl);

/**
 * Authenticate a prefill connection
 * @param username The user name
 * @param password The password
 * @param database The database
 * @param slot The resulting slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_prefill_auth(char* username, char* password, char* database, int* slot);

/**
 * Authenticate a remote management user
 * @param client_fd The descriptor
 * @param address The client address
 * @param client_ssl The client SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_remote_management_auth(int client_fd, char* address, SSL** client_ssl);

/**
 * Connect using SCRAM-SHA256
 * @param username The user name
 * @param password The password
 * @param server_fd The descriptor
 * @param s_ssl The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl);

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
 * @param ciphertext_length The length of the ciphertext
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length);

/**
 * Decrypt a string
 * @param ciphertext The string
 * @param ciphertext_length The length of the ciphertext
 * @param password The master password
 * @param plaintext The plaintext output
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext);

/**
 * MD5 a string
 * @param str The string
 * @param md5 The MD5 string
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_md5(char* str, int length, char** md5);

/**
 * Is the user known to the system
 * @param user The user name
 * @return True if known, otherwise false
 */
bool
pgagroal_user_known(char* user);

/**
 * Is the TLS configuration valid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tls_valid(void);

#ifdef __cplusplus
}
#endif

#endif
