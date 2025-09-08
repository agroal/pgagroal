/*
 * Copyright (C) 2025 The pgagroal community
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

#ifndef PGAGROAL_TSCOMMON_H
#define PGAGROAL_TSCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <stdbool.h>
#include <stddef.h>

#define ENV_VAR_BASE_DIR "PGAGROAL_TEST_BASE_DIR"
#define ENV_VAR_CONF_PATH "PGAGROAL_TEST_CONF_DIR"
#define ENV_VAR_RESTORE_DIR "PGAGROAL_TEST_RESOURCE_DIR"

/*
 * Base directory for tests. Populated by pgagroal_test_setup() from
 * PGAGROAL_TEST_BASE_DIR or derived from project path.
 */
extern char TEST_BASE_DIR[MAX_PATH];

/* Directories under TEST_BASE_DIR provisioned by check.sh */
extern char TEST_CONF_DIR[MAX_PATH];      /* $BASE_DIR/conf */
extern char TEST_RESOURCE_DIR[MAX_PATH];  /* $BASE_DIR/resource */

/* Note: old TEST_RESTORE_DIR and TEST_CONFIG_SAMPLE_PATH removed */

/* Create per-test environment (idempotent) */
void pgagroal_test_setup(void);

/* Teardown per-test environment (currently no-op) */
void pgagroal_test_teardown(void);

/* Explicit helpers if a test needs them */
void pgagroal_test_environment_create(void);
void pgagroal_test_environment_destroy(void);
void pgagroal_test_basedir_cleanup(void);

/* Verify essential directories exist; returns 0 on success */
int pgagroal_test_verify_layout(void);

#ifdef __cplusplus
}
#endif

#endif /* PGAGROAL_TSCOMMON_H */
