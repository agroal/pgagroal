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

#ifndef PGAGROAL_UTILS_H
#define PGAGROAL_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <message.h>

#include <stdlib.h>

/** @struct
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   int slot;                /**< The slot */
};

/**
 * Get the request identifier
 * @param msg The message
 * @return The identifier
 */
int32_t
pgagroal_get_request(struct message* msg);

/**
 * Extract the user name and database from a message
 * @param msg The message
 * @param username The resulting user name
 * @param database The resulting database
 * @param appname The resulting application_name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_username_database(struct message* msg, char** username, char** database, char** appname);

/**
 * Extract a message from a message
 * @param type The message type to be extracted
 * @param msg The message
 * @param extracted The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_message(char type, struct message* msg, struct message** extracted);

/**
 * Extract an error message from a message
 * @param msg The message
 * @param error The error
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_error_message(struct message* msg, char** error);

/**
 * Get a string for the state
 * @param state
 * @return The string
 */
char*
pgagroal_get_state_string(signed char state);

/**
 * Read a byte
 * @param data Pointer to the data
 * @return The byte
 */
signed char
pgagroal_read_byte(void* data);

/**
 * Read an int16
 * @param data Pointer to the data
 * @return The int16
 */
int16_t
pgagroal_read_int16(void* data);

/**
 * Read an int32
 * @param data Pointer to the data
 * @return The int32
 */
int32_t
pgagroal_read_int32(void* data);

/**
 * Read a long
 * @param data Pointer to the data
 * @return The long
 */
long
pgagroal_read_long(void* data);

/**
 * Read a string
 * @param data Pointer to the data
 * @return The string
 */
char*
pgagroal_read_string(void* data);

/**
 * Write a byte
 * @param data Pointer to the data
 * @param b The byte
 */
void
pgagroal_write_byte(void* data, signed char b);

/**
 * Write an int32
 * @param data Pointer to the data
 * @param i The int32
 */
void
pgagroal_write_int32(void* data, int32_t i);

/**
 * Write a long
 * @param data Pointer to the data
 * @param l The long int
 */
void
pgagroal_write_long(void* data, long l);

/**
 * Write a string
 * @param data Pointer to the data
 * @param s The string
 */
void
pgagroal_write_string(void* data, char* s);

/**
 * Is the machine big endian ?
 * @return True if big, otherwise false for little
 */
bool
pgagroal_bigendian(void);

/**
 * Swap
 * @param i The value
 * @return The swapped value
 */
unsigned int
pgagroal_swap(unsigned int i);

/**
 * Print the available libev engines
 */
void
pgagroal_libev_engines(void);

/**
 * Get the constant for a libev engine
 * @param engine The name of the engine
 * @return The constant
 */
unsigned int
pgagroal_libev(char* engine);

/**
 * Get the name for a libev engine
 * @param val The constant
 * @return The name
 */
char*
pgagroal_libev_engine(unsigned int val);

/**
 * Get the home directory
 * @return The directory
 */
char*
pgagroal_get_home_directory(void);

/**
 * Get the user name
 * @return The user name
 */
char*
pgagroal_get_user_name(void);

/**
 * Get a password from stdin
 * @return The password
 */
char*
pgagroal_get_password(void);

/**
 * BASE64 encode a string
 * @param raw The string
 * @param raw_length The length of the raw string
 * @param encoded The encoded string
 * @return 0 if success, otherwise 1
 */
int
pgagroal_base64_encode(char* raw, int raw_length, char** encoded);

/**
 * BASE64 decode a string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @param raw The raw string
 * @param raw_length The length of the raw string
 * @return 0 if success, otherwise 1
 */
int
pgagroal_base64_decode(char* encoded, size_t encoded_length, char** raw, int* raw_length);

/**
 * Set process title.
 *
 * The function will autonomously check the update policy set
 * via the configuration option `update_process_title` and
 * will do nothing if the setting is `never`.
 * In the case the policy is set to `strict`, the process title
 * will not overflow the initial command line length (i.e., strlen(argv[*]))
 * otherwise it will do its best to set the title to the desired string.
 *
 * The policies `strict` and `minimal` will be honored only on Linux platforms
 * where a native call to set the process title is not available.
 *
 *
 * The resulting process title will be set to either `s1` or `s1/s2` if there
 * both strings and the length is allowed by the policy.
 *
 * @param argc The number of arguments
 * @param argv The argv pointer
 * @param s1 The first string
 * @param s2 The second string
 */
void
pgagroal_set_proc_title(int argc, char** argv, char* s1, char* s2);

/**
 * Sets the process title for a given connection.
 *
 * Uses `pgagroal_set_proc_title` to build an information string
 * with the form
 *    user@host:port/database
 *
 * This means that all the policies honored by the latter function and
 * set via the `update_process_title` configuration paramter will be
 * honored.
 *
 * @param argc the number of arguments
 * @param argv command line arguments
 * @param connection the struct connection pointer for the established connection.
 */
void
pgagroal_set_connection_proc_title(int argc, char** argv, struct connection* connection);

/**
 * Provide the application version number as a unique value composed of the three
 * specified parts. For example, when invoked with (1,5,0) it returns 10500.
 * Every part of the number must be between 0 and 99, and the function
 * applies a restriction on the values. For example passing 1 or 101 as one of the part
 * will produce the same result.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns a number made by (patch + minor * 100 + major * 10000 )
 */
unsigned int
pgagroal_version_as_number(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Provides the current version number of the application.
 * It relies on `pgagroal_version_as_number` and invokes it with the
 * predefined constants.
 *
 * @returns the current version number
 */
unsigned int
pgagroal_version_number(void);

/**
 * Checks if the currently running version number is
 * greater or equal than the specied one.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns true if the current version is greater or equal to the specified one
 */
bool
pgagroal_version_ge(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Append a string
 *
 * @param orig The original string
 * @param s The string
 * @returns The new string
 */
char*
pgagroal_append(char* orig, char* s);

/**
 * Append an int
 *
 * @param orig The original string
 * @param i The int
 * @returns The new string
 */
char*
pgagroal_append_int(char* orig, int i);

/**
 * Append an unsigned long
 *
 * @param orig The original string
 * @param l The long
 * @returns The new string
 */
char*
pgagroal_append_ulong(char* orig, unsigned long l);

/**
 * Append an unsigned long long
 *
 * @param orig The original string
 * @param l The long
 * @returns The new string
 */
char*
pgagroal_append_ullong(char* orig, unsigned long long l);

#ifdef DEBUG

/**
 * Generate a backtrace in the log
 * @return 0 if success, otherwise 1
 */
int
pgagroal_backtrace(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
