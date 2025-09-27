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

#include <tsclient.h>
#include <tssuite.h>
#include <stdlib.h>

#define UTF8_USER "utf8user"
#define UTF8_DATABASE "utf8db"

// Simple connection test for UTF-8 user
START_TEST(test_pgagroal_utf8_simple)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(UTF8_USER, UTF8_DATABASE, true, 0, 0, 0);
   ck_assert_msg(found, "Connection to UTF-8 user failed");
}

// Load test for UTF-8 user
START_TEST(test_pgagroal_utf8_load)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(UTF8_USER, UTF8_DATABASE, true, 8, 0, 1000);
   ck_assert_msg(found, "Load test for UTF-8 user failed");
}

Suite*
pgagroal_test_utf8_suite()
{
   Suite* s;
   TCase* tc_core;

   s = suite_create("pgagroal_test_utf8");
   tc_core = tcase_create("Core");

   tcase_set_timeout(tc_core, 60);
   tcase_add_test(tc_core, test_pgagroal_utf8_simple);
   tcase_add_test(tc_core, test_pgagroal_utf8_load);
   suite_add_tcase(s, tc_core);

   return s;
}
