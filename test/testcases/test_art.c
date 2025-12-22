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
 *
 */

#include <pgagroal.h>
#include <art.h>
#include <tsclient.h>
#include <tssuite.h>
#include <utils.h>
#include <value.h>

#include <stdio.h>
#include <string.h>

struct art_test_obj
{
   char* str;
   int idx;
};

static void test_obj_create(int idx, struct art_test_obj** obj);
static void test_obj_destroy(struct art_test_obj* obj);
static void test_obj_destroy_cb(uintptr_t obj);

START_TEST(test_art_create)
{
   struct art* t = NULL;
   pgagroal_art_create(&t);

   ck_assert_ptr_nonnull(t);
   ck_assert_int_eq(t->size, 0);
   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_insert)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   ck_assert_ptr_nonnull(t);

   ck_assert(pgagroal_art_insert(t, "key_none", 0, ValueNone));
   ck_assert(pgagroal_art_insert(t, NULL, 0, ValueInt8));
   ck_assert(pgagroal_art_insert(NULL, "key_none", 0, ValueInt8));

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(!pgagroal_art_insert(t, "key_int", 1, ValueInt32));
   ck_assert(!pgagroal_art_insert(t, "key_bool", true, ValueBool));
   ck_assert(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat));
   ck_assert(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble));
   ck_assert(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem));

   test_obj_create(0, &obj);
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config));
   ck_assert_int_eq(t->size, 7);

   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_search)
{
   struct art* t = NULL;
   struct art_test_obj* obj1 = NULL;
   struct art_test_obj* obj2 = NULL;
   enum value_type type = ValueNone;
   char* value2 = NULL;
   char* key_str = NULL;

   pgagroal_art_create(&t);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   ck_assert_ptr_nonnull(t);

   ck_assert(pgagroal_art_insert(t, "key_none", 0, ValueNone));
   ck_assert(!pgagroal_art_contains_key(t, "key_none"));
   ck_assert_int_eq(pgagroal_art_search(t, "key_none"), 0);
   ck_assert_int_eq(pgagroal_art_search_typed(t, "key_none", &type), 0);
   ck_assert_int_eq(type, ValueNone);

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(pgagroal_art_contains_key(t, "key_str"));
   ck_assert_str_eq((char*)pgagroal_art_search(t, "key_str"), "value1");

   // inserting string makes a copy
   key_str = pgagroal_append(key_str, "key_str");
   value2 = pgagroal_append(value2, "value2");
   ck_assert(!pgagroal_art_insert(t, key_str, (uintptr_t)value2, ValueString));
   ck_assert_str_eq((char*)pgagroal_art_search(t, "key_str"), "value2");
   free(value2);
   free(key_str);

   ck_assert(!pgagroal_art_insert(t, "key_int", -1, ValueInt32));
   ck_assert(pgagroal_art_contains_key(t, "key_int"));
   ck_assert_int_eq((int)pgagroal_art_search(t, "key_int"), -1);

   ck_assert(!pgagroal_art_insert(t, "key_bool", true, ValueBool));
   ck_assert((bool)pgagroal_art_search(t, "key_bool"));

   ck_assert(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat));
   ck_assert(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble));
   ck_assert_float_eq(pgagroal_value_to_float(pgagroal_art_search(t, "key_float")), 2.5);
   ck_assert_double_eq(pgagroal_value_to_double(pgagroal_art_search(t, "key_double")), 2.5);

   test_obj_create(1, &obj1);
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj1, &test_obj_config));
   ck_assert_int_eq(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->idx, 1);
   ck_assert_str_eq(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->str, "obj1");
   pgagroal_art_search_typed(t, "key_obj", &type);
   ck_assert_int_eq(type, ValueRef);

   // test obj overwrite with memory free up
   test_obj_create(2, &obj2);
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj2, &test_obj_config));
   ck_assert_int_eq(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->idx, 2);
   ck_assert_str_eq(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->str, "obj2");

   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_basic_delete)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   ck_assert_ptr_nonnull(t);
   test_obj_create(0, &obj);

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(!pgagroal_art_insert(t, "key_int", 1, ValueInt32));
   ck_assert(!pgagroal_art_insert(t, "key_bool", true, ValueBool));
   ck_assert(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat));
   ck_assert(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble));
   ck_assert(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem));
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config));

   ck_assert(pgagroal_art_contains_key(t, "key_str"));
   ck_assert(pgagroal_art_contains_key(t, "key_int"));
   ck_assert(pgagroal_art_contains_key(t, "key_bool"));
   ck_assert(pgagroal_art_contains_key(t, "key_mem"));
   ck_assert(pgagroal_art_contains_key(t, "key_float"));
   ck_assert(pgagroal_art_contains_key(t, "key_double"));
   ck_assert(pgagroal_art_contains_key(t, "key_obj"));
   ck_assert_int_eq(t->size, 7);

   ck_assert(pgagroal_art_delete(t, NULL));
   ck_assert(pgagroal_art_delete(NULL, "key_str"));

   ck_assert(!pgagroal_art_delete(t, "key_str"));
   ck_assert(!pgagroal_art_contains_key(t, "key_str"));
   ck_assert_int_eq(t->size, 6);

   ck_assert(!pgagroal_art_delete(t, "key_int"));
   ck_assert(!pgagroal_art_contains_key(t, "key_int"));
   ck_assert_int_eq(t->size, 5);

   ck_assert(!pgagroal_art_delete(t, "key_bool"));
   ck_assert(!pgagroal_art_contains_key(t, "key_bool"));
   ck_assert_int_eq(t->size, 4);

   ck_assert(!pgagroal_art_delete(t, "key_mem"));
   ck_assert(!pgagroal_art_contains_key(t, "key_mem"));
   ck_assert_int_eq(t->size, 3);

   ck_assert(!pgagroal_art_delete(t, "key_float"));
   ck_assert(!pgagroal_art_contains_key(t, "key_float"));
   ck_assert_int_eq(t->size, 2);

   ck_assert(!pgagroal_art_delete(t, "key_double"));
   ck_assert(!pgagroal_art_contains_key(t, "key_double"));
   ck_assert_int_eq(t->size, 1);

   ck_assert(!pgagroal_art_delete(t, "key_obj"));
   ck_assert(!pgagroal_art_contains_key(t, "key_obj"));
   ck_assert_int_eq(t->size, 0);

   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_double_delete)
{
   struct art* t = NULL;
   pgagroal_art_create(&t);

   ck_assert_ptr_nonnull(t);

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(!pgagroal_art_insert(t, "key_int", 1, ValueInt32));

   ck_assert(pgagroal_art_contains_key(t, "key_str"));
   ck_assert_int_eq(t->size, 2);

   ck_assert(!pgagroal_art_delete(t, "key_str"));
   ck_assert(!pgagroal_art_contains_key(t, "key_str"));
   ck_assert_int_eq(t->size, 1);

   ck_assert(!pgagroal_art_delete(t, "key_str"));
   ck_assert(!pgagroal_art_contains_key(t, "key_str"));
   ck_assert_int_eq(t->size, 1);

   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_clear)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   ck_assert_ptr_nonnull(t);
   test_obj_create(0, &obj);

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(!pgagroal_art_insert(t, "key_int", 1, ValueInt32));
   ck_assert(!pgagroal_art_insert(t, "key_bool", true, ValueBool));
   ck_assert(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat));
   ck_assert(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble));
   ck_assert(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem));
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config));

   ck_assert(pgagroal_art_contains_key(t, "key_str"));
   ck_assert(pgagroal_art_contains_key(t, "key_int"));
   ck_assert(pgagroal_art_contains_key(t, "key_bool"));
   ck_assert(pgagroal_art_contains_key(t, "key_mem"));
   ck_assert(pgagroal_art_contains_key(t, "key_float"));
   ck_assert(pgagroal_art_contains_key(t, "key_double"));
   ck_assert(pgagroal_art_contains_key(t, "key_obj"));
   ck_assert_int_eq(t->size, 7);

   ck_assert(!pgagroal_art_clear(t));
   ck_assert_int_eq(t->size, 0);
   ck_assert_ptr_null(t->root);

   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_iterator_read)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   ck_assert_ptr_nonnull(t);
   test_obj_create(1, &obj);

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(!pgagroal_art_insert(t, "key_int", 1, ValueInt32));
   ck_assert(!pgagroal_art_insert(t, "key_bool", true, ValueBool));
   ck_assert(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat));
   ck_assert(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble));
   ck_assert(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem));
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config));

   ck_assert(pgagroal_art_iterator_create(NULL, &iter));
   ck_assert_ptr_null(iter);
   ck_assert(!pgagroal_art_iterator_create(t, &iter));
   ck_assert_ptr_nonnull(iter);
   ck_assert(pgagroal_art_iterator_has_next(iter));

   int cnt = 0;
   while (pgagroal_art_iterator_next(iter))
   {
      if (pgagroal_compare_string(iter->key, "key_str"))
      {
         ck_assert_str_eq((char*)pgagroal_value_data(iter->value), "value1");
      }
      else if (pgagroal_compare_string(iter->key, "key_int"))
      {
         ck_assert_int_eq((int)pgagroal_value_data(iter->value), 1);
      }
      else if (pgagroal_compare_string(iter->key, "key_bool"))
      {
         ck_assert((bool)pgagroal_value_data(iter->value));
      }
      else if (pgagroal_compare_string(iter->key, "key_float"))
      {
         ck_assert_float_eq(pgagroal_value_to_float(pgagroal_value_data(iter->value)), 2.5);
      }
      else if (pgagroal_compare_string(iter->key, "key_double"))
      {
         ck_assert_double_eq(pgagroal_value_to_double(pgagroal_value_data(iter->value)), 2.5);
      }
      else if (pgagroal_compare_string(iter->key, "key_mem"))
      {
         // as long as it exists...
         ck_assert(true);
      }
      else if (pgagroal_compare_string(iter->key, "key_obj"))
      {
         ck_assert_int_eq(((struct art_test_obj*)pgagroal_value_data(iter->value))->idx, 1);
         ck_assert_str_eq(((struct art_test_obj*)pgagroal_value_data(iter->value))->str, "obj1");
      }
      else
      {
         ck_assert_msg(false, "found key not inserted: %s", iter->key);
      }

      cnt++;
   }
   ck_assert_int_eq(cnt, t->size);
   ck_assert(!pgagroal_art_iterator_has_next(iter));

   pgagroal_art_iterator_destroy(iter);
   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_iterator_remove)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   ck_assert_ptr_nonnull(t);
   test_obj_create(1, &obj);

   ck_assert(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString));
   ck_assert(!pgagroal_art_insert(t, "key_int", 1, ValueInt32));
   ck_assert(!pgagroal_art_insert(t, "key_bool", true, ValueBool));
   ck_assert(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat));
   ck_assert(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble));
   ck_assert(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem));
   ck_assert(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config));

   ck_assert_int_eq(t->size, 7);

   ck_assert(!pgagroal_art_iterator_create(t, &iter));
   ck_assert_ptr_nonnull(iter);
   ck_assert(pgagroal_art_iterator_has_next(iter));

   int cnt = 0;
   while (pgagroal_art_iterator_next(iter))
   {
      cnt++;
      if (pgagroal_compare_string(iter->key, "key_str"))
      {
         ck_assert_str_eq((char*)pgagroal_value_data(iter->value), "value1");
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_str"));
      }
      else if (pgagroal_compare_string(iter->key, "key_int"))
      {
         ck_assert_int_eq((int)pgagroal_value_data(iter->value), 1);
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_int"));
      }
      else if (pgagroal_compare_string(iter->key, "key_bool"))
      {
         ck_assert((bool)pgagroal_value_data(iter->value));
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_bool"));
      }
      else if (pgagroal_compare_string(iter->key, "key_float"))
      {
         ck_assert_float_eq(pgagroal_value_to_float(pgagroal_value_data(iter->value)), 2.5);
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_float"));
      }
      else if (pgagroal_compare_string(iter->key, "key_double"))
      {
         ck_assert_double_eq(pgagroal_value_to_double(pgagroal_value_data(iter->value)), 2.5);
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_double"));
      }
      else if (pgagroal_compare_string(iter->key, "key_mem"))
      {
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_mem"));
      }
      else if (pgagroal_compare_string(iter->key, "key_obj"))
      {
         ck_assert_int_eq(((struct art_test_obj*)pgagroal_value_data(iter->value))->idx, 1);
         ck_assert_str_eq(((struct art_test_obj*)pgagroal_value_data(iter->value))->str, "obj1");
         pgagroal_art_iterator_remove(iter);
         ck_assert(!pgagroal_art_contains_key(t, "key_obj"));
      }
      else
      {
         ck_assert_msg(false, "found key not inserted: %s", iter->key);
      }

      ck_assert_int_eq(t->size, 7 - cnt);
      ck_assert_ptr_null(iter->key);
      ck_assert_ptr_null(iter->value);
   }
   ck_assert_int_eq(cnt, 7);
   ck_assert_int_eq(t->size, 0);
   ck_assert(!pgagroal_art_iterator_has_next(iter));

   pgagroal_art_iterator_destroy(iter);
   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_insert_search_extensive)
{
   struct art* t = NULL;
   char buf[512];
   FILE* f = NULL;
   uintptr_t line = 1;
   int len = 0;
   char* path = NULL;
   path = pgagroal_append(path, project_directory);
   path = pgagroal_append(path, "/pgagroal-testsuite/resource/art_advanced_test/words.txt");

   f = fopen(path, "r");
   ck_assert_ptr_nonnull(f);

   pgagroal_art_create(&t);
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      ck_assert(!pgagroal_art_insert(t, buf, line, ValueInt32));
      line++;
   }

   // Seek back to the start
   fseek(f, 0, SEEK_SET);
   line = 1;
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      int val = (int)pgagroal_art_search(t, buf);
      ck_assert_msg(val == line, "test_art_insert_search_advanced Line: %d Val: %d Str: %s\n", (int)line, val, buf);
      line++;
   }

   fclose(f);
   free(path);
   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_insert_very_long)
{
   struct art* t;
   pgagroal_art_create(&t);

   unsigned char key1[300] = {16, 1, 1, 1, 7, 11, 1, 1, 1, 2, 17, 11, 1, 1, 1, 121, 11, 1, 1, 1, 121, 11, 1,
                              1, 1, 216, 11, 1, 1, 1, 202, 11, 1, 1, 1, 194, 11, 1, 1, 1, 224, 11, 1, 1, 1,
                              231, 11, 1, 1, 1, 211, 11, 1, 1, 1, 206, 11, 1, 1, 1, 208, 11, 1, 1, 1, 232,
                              11, 1, 1, 1, 124, 11, 1, 1, 1, 124, 2, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 210, 95, 185, 89, 111, 118, 250, 173, 202, 199, 101, 1,
                              8, 18, 182, 92, 236, 147, 171, 101, 151, 195, 112, 185, 218, 108, 246,
                              139, 164, 234, 195, 58, 177, 1, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 250, 173, 202, 199, 101, 1,
                              8, 18, 181, 93, 46, 150, 9, 212, 191, 95, 102, 178, 217, 44, 178, 235,
                              29, 191, 218, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213, 251, 173, 202,
                              211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1, 8, 18, 181, 93,
                              46, 151, 9, 212, 191, 95, 102, 183, 219, 229, 214, 59, 125, 182, 71,
                              108, 181, 220, 238, 150, 91, 117, 151, 201, 84, 183, 128, 8, 16, 1, 1,
                              1, 2, 12, 185, 89, 44, 213, 251, 173, 202, 211, 95, 185, 89, 111, 118,
                              251, 173, 202, 199, 100, 1, 8, 18, 181, 93, 46, 151, 9, 212, 191, 95,
                              108, 176, 217, 47, 51, 219, 61, 134, 207, 97, 151, 88, 237, 246, 208,
                              8, 18, 255, 255, 255, 219, 191, 198, 134, 5, 223, 212, 72, 44, 208,
                              251, 181, 14, 1, 1, 1, 8, '\0'};
   unsigned char key2[303] = {16, 1, 1, 1, 7, 10, 1, 1, 1, 2, 17, 11, 1, 1, 1, 121, 11, 1, 1, 1, 121, 11, 1,
                              1, 1, 216, 11, 1, 1, 1, 202, 11, 1, 1, 1, 194, 11, 1, 1, 1, 224, 11, 1, 1, 1,
                              231, 11, 1, 1, 1, 211, 11, 1, 1, 1, 206, 11, 1, 1, 1, 208, 11, 1, 1, 1, 232,
                              11, 1, 1, 1, 124, 10, 1, 1, 1, 124, 2, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1,
                              8, 18, 182, 92, 236, 147, 171, 101, 150, 195, 112, 185, 218, 108, 246,
                              139, 164, 234, 195, 58, 177, 1, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1,
                              8, 18, 181, 93, 46, 151, 9, 212, 191, 95, 102, 178, 217, 44, 178, 235,
                              29, 191, 218, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213, 251, 173, 202,
                              211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1, 8, 18, 181, 93,
                              46, 151, 9, 212, 191, 95, 102, 183, 219, 229, 214, 59, 125, 182, 71,
                              108, 181, 221, 238, 151, 91, 117, 151, 201, 84, 183, 128, 8, 16, 1, 1,
                              1, 3, 12, 185, 89, 44, 213, 250, 133, 178, 195, 105, 183, 87, 237, 151,
                              155, 165, 151, 229, 97, 182, 1, 8, 18, 161, 91, 239, 51, 11, 61, 151,
                              223, 114, 179, 217, 64, 8, 12, 186, 219, 172, 151, 91, 53, 166, 221,
                              101, 178, 1, 8, 18, 255, 255, 255, 219, 191, 198, 134, 5, 208, 212, 72,
                              44, 208, 251, 180, 14, 1, 1, 1, 8, '\0'};

   ck_assert(!pgagroal_art_insert(t, (char*)key1, (uintptr_t)key1, ValueRef));
   ck_assert(!pgagroal_art_insert(t, (char*)key2, (uintptr_t)key2, ValueRef));
   ck_assert(!pgagroal_art_insert(t, (char*)key2, (uintptr_t)key2, ValueRef));
   ck_assert_int_eq(t->size, 2);

   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_random_delete)
{
   struct art* t = NULL;
   char buf[512];
   FILE* f = NULL;
   uintptr_t line = 1;
   int len = 0;
   char* path = NULL;
   path = pgagroal_append(path, project_directory);
   path = pgagroal_append(path, "/pgagroal-testsuite/resource/art_advanced_test/words.txt");

   f = fopen(path, "r");
   ck_assert_ptr_nonnull(f);

   pgagroal_art_create(&t);
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      ck_assert(!pgagroal_art_insert(t, buf, line, ValueInt32));
      line++;
   }

   // Seek back to the start
   fseek(f, 0, SEEK_SET);
   line = 1;
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      int val = (int)pgagroal_art_search(t, buf);
      ck_assert_msg(val == line, "test_art_insert_search_advanced Line: %d Val: %d Str: %s\n", (int)line, val, buf);
      line++;
   }

   ck_assert(!pgagroal_art_delete(t, "A"));
   ck_assert(!pgagroal_art_contains_key(t, "A"));

   ck_assert(!pgagroal_art_delete(t, "yard"));
   ck_assert(!pgagroal_art_contains_key(t, "yard"));

   ck_assert(!pgagroal_art_delete(t, "Xenarchi"));
   ck_assert(!pgagroal_art_contains_key(t, "Xenarchi"));

   ck_assert(!pgagroal_art_delete(t, "F"));
   ck_assert(!pgagroal_art_contains_key(t, "F"));

   ck_assert(!pgagroal_art_delete(t, "wirespun"));
   ck_assert(!pgagroal_art_contains_key(t, "wirespun"));

   fclose(f);
   free(path);
   pgagroal_art_destroy(t);
}
END_TEST
START_TEST(test_art_insert_index_out_of_range)
{
   struct art* t;
   pgagroal_art_create(&t);
   char* s1 = "abcdefghijklmnxyz";
   char* s2 = "abcdefghijklmnopqrstuvw";
   char* s3 = "abcdefghijk";
   ck_assert(!pgagroal_art_insert(t, s1, 1, ValueUInt8));
   ck_assert(!pgagroal_art_insert(t, s2, 1, ValueUInt8));
   ck_assert_int_eq(pgagroal_art_search(t, s3), 0);
   pgagroal_art_destroy(t);
}
END_TEST

Suite*
pgagroal_test_art_suite()
{
   Suite* s;
   TCase* tc_art_basic;
   TCase* tc_art_advanced;

   s = suite_create("pgagroal_test_art");

   tc_art_basic = tcase_create("art_basic_test");
   tcase_set_timeout(tc_art_basic, 60);
   tcase_add_test(tc_art_basic, test_art_create);
   tcase_add_test(tc_art_basic, test_art_insert);
   tcase_add_test(tc_art_basic, test_art_search);
   tcase_add_test(tc_art_basic, test_art_basic_delete);
   tcase_add_test(tc_art_basic, test_art_double_delete);
   tcase_add_test(tc_art_basic, test_art_clear);
   tcase_add_test(tc_art_basic, test_art_iterator_read);
   tcase_add_test(tc_art_basic, test_art_iterator_remove);

   tc_art_advanced = tcase_create("art_advanced_test");
   tcase_set_timeout(tc_art_advanced, 60);
   tcase_add_test(tc_art_advanced, test_art_insert_search_extensive);
   tcase_add_test(tc_art_advanced, test_art_insert_very_long);
   tcase_add_test(tc_art_advanced, test_art_random_delete);
   tcase_add_test(tc_art_advanced, test_art_insert_index_out_of_range);

   suite_add_tcase(s, tc_art_basic);
   suite_add_tcase(s, tc_art_advanced);

   return s;
}

static void
test_obj_create(int idx, struct art_test_obj** obj)
{
   struct art_test_obj* o = NULL;

   o = malloc(sizeof(struct art_test_obj));
   memset(o, 0, sizeof(struct art_test_obj));
   o->idx = idx;
   o->str = pgagroal_append(o->str, "obj");
   o->str = pgagroal_append_int(o->str, idx);

   *obj = o;
}
static void
test_obj_destroy(struct art_test_obj* obj)
{
   if (obj == NULL)
   {
      return;
   }
   free(obj->str);
   free(obj);
}

static void
test_obj_destroy_cb(uintptr_t obj)
{
   test_obj_destroy((struct art_test_obj*)obj);
}
