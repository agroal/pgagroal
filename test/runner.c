/*
 * Copyright (C) 2026 The pgagroal community
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

char* user = NULL;
char* database = NULL;

int
main(int argc, char* argv[])
{
   if (argc != 4)
   {
      printf("Usage: %s <project_directory> <user> <database>\n", argv[0]);
      return 1;
   }

   int number_failed;
   Suite* connection_suite;
   Suite* alias_suite;
   Suite* art_suite;
   Suite* deque_suite;
   Suite* json_suite;
   Suite* utf8_suite;
   SRunner* sr;

   user = strdup(argv[2]);
   database = strdup(argv[3]);

   if (pgagroal_tsclient_init(argv[1]))
   {
      goto error;
   }

   connection_suite = pgagroal_test_connection_suite();
   alias_suite = pgagroal_test_alias_suite();
   utf8_suite = pgagroal_test_utf8_suite();
   art_suite = pgagroal_test_art_suite();
   deque_suite = pgagroal_test_deque_suite();
   json_suite = pgagroal_test_json_suite();

   sr = srunner_create(connection_suite);
   srunner_add_suite(sr, alias_suite);
   srunner_add_suite(sr, art_suite);
   srunner_add_suite(sr, deque_suite);
   srunner_add_suite(sr, json_suite);
   srunner_add_suite(sr, utf8_suite);

   // Run the tests in verbose mode
   srunner_run_all(sr, CK_VERBOSE);
   srunner_set_log(sr, "-");
   number_failed = srunner_ntests_failed(sr);
   srunner_free(sr);
   free(user);
   free(database);

   pgagroal_tsclient_destroy();
   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;

error:
   pgagroal_tsclient_destroy();
   free(user);
   free(database);
   return EXIT_FAILURE;
}