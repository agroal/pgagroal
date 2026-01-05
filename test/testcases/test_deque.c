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
 *
 */
#include <pgagroal.h>
#include <deque.h>
#include <tssuite.h>
#include <utils.h>
#include <value.h>

struct deque_test_obj
{
   char* str;
   int idx;
};

static void test_obj_create(int idx, struct deque_test_obj** obj);
static void test_obj_destroy(struct deque_test_obj* obj);
static void test_obj_destroy_cb(uintptr_t obj);

START_TEST(test_deque_create)
{
   struct deque* dq = NULL;

   ck_assert(!pgagroal_deque_create(false, &dq));
   ck_assert_ptr_nonnull(dq);
   ck_assert_int_eq(dq->size, 0);

   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_add_poll)
{
   struct deque* dq = NULL;

   pgagroal_deque_create(false, &dq);
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32));
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t) true, ValueBool));
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t) "value1", ValueString));
   ck_assert_int_eq(dq->size, 3);

   ck_assert_int_eq((int)pgagroal_deque_peek(dq, NULL), -1);

   ck_assert_int_eq((int)pgagroal_deque_poll(dq, NULL), -1);
   ck_assert_int_eq(dq->size, 2);

   ck_assert((bool)pgagroal_deque_poll(dq, NULL));
   ck_assert_int_eq(dq->size, 1);

   char* value1 = (char*)pgagroal_deque_poll(dq, NULL);
   ck_assert_str_eq(value1, "value1");
   ck_assert_int_eq(dq->size, 0);
   free(value1);

   ck_assert_int_eq(pgagroal_deque_poll(dq, NULL), 0);
   ck_assert_int_eq(dq->size, 0);

   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_add_poll_last)
{
   struct deque* dq = NULL;

   pgagroal_deque_create(false, &dq);
   pgagroal_deque_add(dq, NULL, 0, ValueNone);
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t) "value1", ValueString));
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t) true, ValueBool));
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32));
   ck_assert_int_eq(dq->size, 3);

   ck_assert_int_eq((int)pgagroal_deque_peek_last(dq, NULL), -1);

   ck_assert_int_eq((int)pgagroal_deque_poll_last(dq, NULL), -1);
   ck_assert_int_eq(dq->size, 2);

   ck_assert((bool)pgagroal_deque_poll_last(dq, NULL));
   ck_assert_int_eq(dq->size, 1);

   char* value1 = (char*)pgagroal_deque_poll_last(dq, NULL);
   ck_assert_str_eq(value1, "value1");
   ck_assert_int_eq(dq->size, 0);
   free(value1);

   ck_assert_int_eq(pgagroal_deque_poll_last(dq, NULL), 0);
   ck_assert_int_eq(dq->size, 0);

   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_clear)
{
   struct deque* dq = NULL;

   pgagroal_deque_create(false, &dq);
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t) "value1", ValueString));
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t) true, ValueBool));
   ck_assert(!pgagroal_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32));
   ck_assert_int_eq(dq->size, 3);

   pgagroal_deque_clear(dq);
   ck_assert_int_eq(dq->size, 0);
   ck_assert_int_eq(pgagroal_deque_poll(dq, NULL), 0);

   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_remove)
{
   struct deque* dq = NULL;
   char* value1 = NULL;
   char* tag = NULL;

   pgagroal_deque_create(false, &dq);
   ck_assert(!pgagroal_deque_add(dq, "tag1", (uintptr_t) "value1", ValueString));
   ck_assert(!pgagroal_deque_add(dq, "tag2", (uintptr_t) true, ValueBool));
   ck_assert(!pgagroal_deque_add(dq, "tag2", (uintptr_t)-1, ValueInt32));
   ck_assert_int_eq(dq->size, 3);

   ck_assert_int_eq(pgagroal_deque_remove(dq, NULL), 0);
   ck_assert_int_eq(pgagroal_deque_remove(NULL, "tag2"), 0);
   ck_assert_int_eq(pgagroal_deque_remove(dq, "tag3"), 0);

   ck_assert_int_eq(pgagroal_deque_remove(dq, "tag2"), 2);
   ck_assert_int_eq(dq->size, 1);

   value1 = (char*)pgagroal_deque_peek(dq, &tag);
   ck_assert_str_eq(value1, "value1");
   ck_assert_str_eq(tag, "tag1");

   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_add_with_config_and_get)
{
   struct deque* dq = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};
   struct deque_test_obj* obj1 = NULL;
   struct deque_test_obj* obj2 = NULL;
   struct deque_test_obj* obj3 = NULL;

   test_obj_create(1, &obj1);
   test_obj_create(2, &obj2);
   test_obj_create(3, &obj3);

   pgagroal_deque_create(false, &dq);
   ck_assert(!pgagroal_deque_add_with_config(dq, "tag1", (uintptr_t)obj1, &test_obj_config));
   ck_assert(!pgagroal_deque_add_with_config(dq, "tag2", (uintptr_t)obj2, &test_obj_config));
   ck_assert(!pgagroal_deque_add_with_config(dq, "tag3", (uintptr_t)obj3, &test_obj_config));
   ck_assert_int_eq(dq->size, 3);

   ck_assert_int_eq(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag1"))->idx, 1);
   ck_assert_str_eq(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag1"))->str, "obj1");

   ck_assert_int_eq(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag2"))->idx, 2);
   ck_assert_str_eq(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag2"))->str, "obj2");

   ck_assert_int_eq(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag3"))->idx, 3);
   ck_assert_str_eq(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag3"))->str, "obj3");

   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_iterator_read)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};

   pgagroal_deque_create(false, &dq);
   ck_assert(!pgagroal_deque_add(dq, "1", 1, ValueInt32));
   ck_assert(!pgagroal_deque_add(dq, "2", 2, ValueInt32));
   ck_assert(!pgagroal_deque_add(dq, "3", 3, ValueInt32));
   ck_assert_int_eq(dq->size, 3);

   ck_assert(pgagroal_deque_iterator_create(NULL, &iter));
   ck_assert(!pgagroal_deque_iterator_create(dq, &iter));
   ck_assert_ptr_nonnull(iter);
   ck_assert(pgagroal_deque_iterator_has_next(iter));

   while (pgagroal_deque_iterator_next(iter))
   {
      cnt++;
      ck_assert_int_eq(pgagroal_value_data(iter->value), cnt);
      tag[0] = '0' + cnt;
      ck_assert_str_eq(iter->tag, tag);
   }
   ck_assert_int_eq(cnt, 3);
   ck_assert(!pgagroal_deque_iterator_has_next(iter));

   pgagroal_deque_iterator_destroy(iter);
   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_iterator_remove)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};

   pgagroal_deque_create(false, &dq);
   ck_assert(!pgagroal_deque_add(dq, "1", 1, ValueInt32));
   ck_assert(!pgagroal_deque_add(dq, "2", 2, ValueInt32));
   ck_assert(!pgagroal_deque_add(dq, "3", 3, ValueInt32));
   ck_assert_int_eq(dq->size, 3);

   ck_assert(pgagroal_deque_iterator_create(NULL, &iter));
   ck_assert(!pgagroal_deque_iterator_create(dq, &iter));
   ck_assert_ptr_nonnull(iter);
   ck_assert(pgagroal_deque_iterator_has_next(iter));

   while (pgagroal_deque_iterator_next(iter))
   {
      cnt++;
      ck_assert_int_eq(pgagroal_value_data(iter->value), cnt);
      tag[0] = '0' + cnt;
      ck_assert_str_eq(iter->tag, tag);

      if (cnt == 2 || cnt == 3)
      {
         pgagroal_deque_iterator_remove(iter);
      }
   }

   // should be no-op
   pgagroal_deque_iterator_remove(iter);

   ck_assert_int_eq(dq->size, 1);
   ck_assert(!pgagroal_deque_iterator_has_next(iter));

   ck_assert_int_eq(pgagroal_deque_peek(dq, NULL), 1);

   pgagroal_deque_iterator_destroy(iter);
   pgagroal_deque_destroy(dq);
}
END_TEST
START_TEST(test_deque_sort)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};
   int index[6] = {2, 1, 3, 5, 4, 0};

   pgagroal_deque_create(false, &dq);
   for (int i = 0; i < 6; i++)
   {
      tag[0] = '0' + index[i];
      ck_assert(!pgagroal_deque_add(dq, tag, index[i], ValueInt32));
   }

   pgagroal_deque_sort(dq);

   pgagroal_deque_iterator_create(dq, &iter);

   while (pgagroal_deque_iterator_next(iter))
   {
      ck_assert_int_eq(pgagroal_value_data(iter->value), cnt);
      tag[0] = '0' + cnt;
      ck_assert_str_eq(iter->tag, tag);
      cnt++;
   }

   pgagroal_deque_iterator_destroy(iter);
   pgagroal_deque_destroy(dq);
}
END_TEST

Suite*
pgagroal_test_deque_suite()
{
   Suite* s;
   TCase* tc_deque_basic;

   s = suite_create("pgagroal_test_deque");

   tc_deque_basic = tcase_create("deque_basic_test");
   tcase_set_timeout(tc_deque_basic, 60);
   tcase_add_test(tc_deque_basic, test_deque_create);
   tcase_add_test(tc_deque_basic, test_deque_add_poll);
   tcase_add_test(tc_deque_basic, test_deque_add_poll_last);
   tcase_add_test(tc_deque_basic, test_deque_remove);
   tcase_add_test(tc_deque_basic, test_deque_add_with_config_and_get);
   tcase_add_test(tc_deque_basic, test_deque_clear);
   tcase_add_test(tc_deque_basic, test_deque_iterator_read);
   tcase_add_test(tc_deque_basic, test_deque_iterator_remove);
   tcase_add_test(tc_deque_basic, test_deque_sort);

   suite_add_tcase(s, tc_deque_basic);

   return s;
}

static void
test_obj_create(int idx, struct deque_test_obj** obj)
{
   struct deque_test_obj* o = NULL;

   o = malloc(sizeof(struct deque_test_obj));
   memset(o, 0, sizeof(struct deque_test_obj));
   o->idx = idx;
   o->str = pgagroal_append(o->str, "obj");
   o->str = pgagroal_append_int(o->str, idx);

   *obj = o;
}
static void
test_obj_destroy(struct deque_test_obj* obj)
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
   test_obj_destroy((struct deque_test_obj*)obj);
}