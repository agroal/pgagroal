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

#include <tscommon.h>
#include<pgagroal.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utils.h>

char TEST_BASE_DIR[MAX_PATH] = {0};
char TEST_CONF_DIR[MAX_PATH] = {0};
char TEST_RESOURCE_DIR[MAX_PATH] = {0};


void
pgagroal_test_environment_create(void)
{
   char* base_dir = getenv(ENV_VAR_BASE_DIR);
   char* res_dir = getenv(ENV_VAR_RESTORE_DIR);
   char* conf_dir = getenv(ENV_VAR_CONF_PATH);


   memset(TEST_BASE_DIR, 0, sizeof(TEST_BASE_DIR));
   memset(TEST_CONF_DIR, 0, sizeof(TEST_CONF_DIR));
   memset(TEST_RESOURCE_DIR, 0, sizeof(TEST_RESOURCE_DIR));
   
   base_dir = getenv(ENV_VAR_BASE_DIR);
   assert(base_dir != NULL);

   memcpy(TEST_BASE_DIR, base_dir, strlen(base_dir) + 1);

   res_dir = getenv(ENV_VAR_RESTORE_DIR);
   assert(res_dir != NULL);
   
   memcpy(TEST_RESOURCE_DIR, res_dir, strlen(res_dir) + 1);

   conf_dir = getenv(ENV_VAR_CONF_PATH);
   assert(conf_dir != NULL);

   memcpy(TEST_CONF_DIR, conf_dir, strlen(conf_dir) + 1);
   
}

void
pgagroal_test_environment_destroy(void)
{

   TEST_BASE_DIR[0] = '\0';
   TEST_CONF_DIR[0] = '\0';
   TEST_RESOURCE_DIR[0] = '\0';
}

void
pgagroal_test_setup(void)
{
   if (TEST_BASE_DIR[0] == '\0')
   {
      pgagroal_test_environment_create();
   }
}

void
pgagroal_test_teardown(void)
{
   /* Uncomment to remove per test:
    * pgagroal_test_environment_destroy();
    */
}

int
pgagroal_test_verify_layout(void)
{
   struct stat st;
   if (TEST_BASE_DIR[0] == '\0')
   {
      return -1;
   }
   if (stat(TEST_BASE_DIR, &st) != 0 || !S_ISDIR(st.st_mode))
   {
      return -1;
   }
   if (TEST_CONF_DIR[0] != '\0')
   {
      if (stat(TEST_CONF_DIR, &st) != 0 || !S_ISDIR(st.st_mode))
      {
         return -1;
      }
   }
   if (TEST_RESOURCE_DIR[0] != '\0')
   {
      if (stat(TEST_RESOURCE_DIR, &st) != 0 || !S_ISDIR(st.st_mode))
      {
         return -1;
      }
   }
   return 0;
}
