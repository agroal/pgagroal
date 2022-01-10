/*
 * Copyright (C) 2022 Red Hat
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

#ifndef PGAGROAL_LOGGING_H
#define PGAGROAL_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#define PGAGROAL_LOGGING_TYPE_CONSOLE 0
#define PGAGROAL_LOGGING_TYPE_FILE    1
#define PGAGROAL_LOGGING_TYPE_SYSLOG  2

#define PGAGROAL_LOGGING_LEVEL_DEBUG5  1
#define PGAGROAL_LOGGING_LEVEL_DEBUG4  1
#define PGAGROAL_LOGGING_LEVEL_DEBUG3  1
#define PGAGROAL_LOGGING_LEVEL_DEBUG2  1
#define PGAGROAL_LOGGING_LEVEL_DEBUG1  2
#define PGAGROAL_LOGGING_LEVEL_INFO    3
#define PGAGROAL_LOGGING_LEVEL_WARN    4
#define PGAGROAL_LOGGING_LEVEL_ERROR   5
#define PGAGROAL_LOGGING_LEVEL_FATAL   6

#define PGAGROAL_LOGGING_MODE_CREATE 0
#define PGAGROAL_LOGGING_MODE_APPEND 1

#define pgagroal_log_trace(...) pgagroal_log_line(PGAGROAL_LOGGING_LEVEL_DEBUG5, __FILE__, __LINE__, __VA_ARGS__)
#define pgagroal_log_debug(...) pgagroal_log_line(PGAGROAL_LOGGING_LEVEL_DEBUG1, __FILE__, __LINE__, __VA_ARGS__)
#define pgagroal_log_info(...)  pgagroal_log_line(PGAGROAL_LOGGING_LEVEL_INFO,  __FILE__, __LINE__,  __VA_ARGS__)
#define pgagroal_log_warn(...)  pgagroal_log_line(PGAGROAL_LOGGING_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define pgagroal_log_error(...) pgagroal_log_line(PGAGROAL_LOGGING_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define pgagroal_log_fatal(...) pgagroal_log_line(PGAGROAL_LOGGING_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

/**
 * Initialize the logging system
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_init_logging(void);

/**
 * Start the logging system
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_start_logging(void);

/**
 * Stop the logging system
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_stop_logging(void);

void
pgagroal_log_line(int level, char *file, int line, char *fmt, ...);

void
pgagroal_log_mem(void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif

