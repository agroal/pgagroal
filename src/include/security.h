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
 * @param server_ssl The server SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_authenticate(int client_fd, char* address, int* slot, SSL** client_ssl, SSL** server_ssl);

/**
 * Authenticate a prefill connection
 * @param username The user name
 * @param password The password
 * @param database The database
 * @param slot The resulting slot
 * @param server_ssl The server SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_prefill_auth(char* username, char* password, char* database, int* slot, SSL** server_ssl);

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

/**
 * @brief Generate a random ASCII password have size of pwd_length
 * @param password the resultant password
 * @param password_length length of the password
 * @return 0 if success, otherwise 1
 */
int
pgagroal_generate_password(int password_length, char** password);

/**
 * @brief Accept the SSL connection for the vault from client (curl)
 * @param config the vault configuration
 * @param client_fd the descriptor
 * @param c_ssl the client SSL context
 * @return 0 if success, otherwise 1
 */
int
accept_ssl_vault(struct vault_configuration* config, int client_fd, SSL** c_ssl);

/**
 * @brief Initialize RNG
 *
 */
void
pgagroal_initialize_random(void);
#ifdef __cplusplus
}
#endif

#endif
