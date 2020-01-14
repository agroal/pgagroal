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

/* pgagroal */
#include <pgagroal.h>
#include <security.h>
#include <utils.h>

#define ZF_LOG_TAG "admin"
#include <zf_log.h>

/* system */
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define ACTION_UNKNOWN     0
#define ACTION_MASTER_KEY  1
#define ACTION_ADD_USER    2
#define ACTION_UPDATE_USER 3
#define ACTION_REMOVE_USER 4
#define ACTION_LIST_USERS  5

static int master_key(char* password);
static bool is_valid_key(char* key);
static int add_user(char* users_path, char* username, char* password);
static int update_user(char* users_path, char* username, char* password);
static int remove_user(char* users_path, char* username);
static int list_users(char* users_path);

static void
version()
{
   printf("pgagroal-admin %s\n", VERSION);
   exit(1);
}

static void
usage()
{
   printf("pgagroal-admin %s\n", VERSION);
   printf("  Administration utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-admin [ -u USERS_FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -U, --user user         Set the user name\n");
   printf("  -P, --password password Set the password for the user\n");
   printf("  -u, --users USERS_FILE  Set the path to the pgagroal_users.conf file\n");
   printf("  -V, --version           Display version information\n");
   printf("  -?, --help              Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  master-key              Create or update the master key\n");
   printf("  add-user                Add a user\n");
   printf("  update-user             Update a user\n");
   printf("  remove-user             Remove a user\n");
   printf("  list-users              List all users\n");
   printf("\n");
}

int
main(int argc, char **argv)
{
   int exit_code = 0;
   int c;
   char* username = NULL;
   char* password = NULL;
   char* users_path = NULL;
   int option_index = 0;
   int32_t action = ACTION_UNKNOWN;

   while (1)
   {
      static struct option long_options[] =
      {
         {"user",  required_argument, 0, 'U'},
         {"password",  required_argument, 0, 'P'},
         {"users",  required_argument, 0, 'u'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "V?u:U:P:",
                      long_options, &option_index);

      if (c == -1)
         break;

      switch (c)
      {
         case 'U':
            username = optarg;
            break;
         case 'P':
            password = optarg;
            break;
         case 'u':
            users_path = optarg;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(1);
            break;
         default:
            break;
      }
   }

   if (argc > 0)
   {
      if (!strcmp("master-key", argv[argc - 1]))
      {
         action = ACTION_MASTER_KEY;
      }
      else if (!strcmp("add-user", argv[argc - 1]))
      {
         action = ACTION_ADD_USER;
      }
      else if (!strcmp("update-user", argv[argc - 1]))
      {
         action = ACTION_UPDATE_USER;
      }
      else if (!strcmp("remove-user", argv[argc - 1]))
      {
         action = ACTION_REMOVE_USER;
      }
      else if (!strcmp("list-users", argv[argc - 1]))
      {
         action = ACTION_LIST_USERS;
      }

      if (action == ACTION_MASTER_KEY)
      {
         if (master_key(password))
         {
            printf("Error for master key\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_ADD_USER)
      {
         if (users_path != NULL)
         {
            if (add_user(users_path, username, password))
            {
               printf("Error for add-user\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing users file argument\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_UPDATE_USER)
      {
         if (users_path != NULL)
         {
            if (update_user(users_path, username, password))
            {
               printf("Error for update-user\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing users file argument\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_REMOVE_USER)
      {
         if (users_path != NULL)
         {
            if (remove_user(users_path, username))
            {
               printf("Error for remove-user\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing users file argument\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_LIST_USERS)
      {
         if (users_path != NULL)
         {
            if (list_users(users_path))
            {
               printf("Error for list-users\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing users file argument\n");
            exit_code = 1;
         }
      }
   }

   if (action == ACTION_UNKNOWN)
   {
      usage();
      exit_code = 1;
   }
   
   return exit_code;
}

static int
master_key(char* password)
{
   FILE* file = NULL;
   char buf[MISC_LENGTH];
   char key[IDENTIFIER_LENGTH];
   char* encoded = NULL;
   struct stat st = {0};

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgagroal", pgagroal_get_home_directory());
   
   if (stat(&buf[0], &st) == -1)
   {
      mkdir(&buf[0], S_IRWXU);
   }
   else
   {
      if (S_ISDIR(st.st_mode) && st.st_mode & S_IRWXU && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         printf("Wrong permissions for .pgagroal\n");
         goto error;
      }
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgagroal/master.key", pgagroal_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      /* Ok */
   }
   else
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         printf("Wrong permissions for master.key\n");
         goto error;
      }
   }


   file = fopen(&buf[0], "w+");

   if (password == NULL)
   {
      memset(&key, 0, sizeof(key));

      while (!is_valid_key(key))
      {
         printf("Master key: ");

         memset(&key, 0, sizeof(key));
         fgets(&key[0], sizeof(key), stdin);
         key[strlen(key) - 1] = 0;
      }

      password = &key[0];
   }
   else
   {
      if (!is_valid_key(password))
      {
         goto error;
      }
   }

   pgagroal_base64_encode(password, &encoded);
   fputs(encoded, file);
   free(encoded);

   fclose(file);

   chmod(&buf[0], S_IRUSR | S_IWUSR);

   return 0;

error:

   free(encoded);

   if (file)
   {
      fclose(file);
   }

   return 1;
}

static bool
is_valid_key(char* key)
{
   bool alpha_lower = false;
   bool alpha_upper = false;
   bool digit = false;
   bool special = false;
   int index;
   char c;
   
   index = 0;

   if (!key)
   {
      return false;
   }

   if (strlen(key) < 8)
   {
      return false;
   }

   while ((c = *(key + index)) != 0)
   {
      if (isalpha(c))
      {
         if (islower(c))
         {
            alpha_lower = true;
         }
         else
         {
            alpha_upper = true;
         }
      }
      else if (isdigit(c))
      {
         digit = true;
      }
      else
      {
         if ((c == '!') || (c == '@') || (c == '#') || (c == '$') || (c == '%') ||
             (c == '^') || (c == '&') || (c == '*') || (c == '(') || (c == ')') ||
             (c == '-') || (c == '_') || (c == '=') || (c == '+') ||
             (c == '[') || (c == '{') || (c == ']') || (c == '}') ||
             (c == '\\') || (c == '|') || (c == ';') || (c == ':') ||
             (c == '\'') || (c == '\"') || (c == ',') || (c == '<') ||
             (c == '.') || (c == '>') || (c == '/') || (c == '?'))
         {
            special = true;
         }
         else
         {
            return false;
         }
      }
      
      index++;
   }

   return alpha_lower && alpha_upper && digit && special;
}

static int
add_user(char* users_path, char* username, char* password)
{
   FILE* users_file = NULL;
   char line[MISC_LENGTH];
   char* master_key = NULL;
   char* ptr = NULL;
   char* encrypted = NULL;
   char* encoded = NULL;
   char un[IDENTIFIER_LENGTH];
   char pwd[IDENTIFIER_LENGTH];
   int number_of_users = 0;

   if (pgagroal_get_master_key(&master_key))
   {
      printf("Invalid master key\n");
      goto error;
   }

   users_file = fopen(users_path, "a+");

   /* User */
   if (username == NULL)
   {
      printf("User name: ");
   
      memset(&un, 0, sizeof(un));
      fgets(&un[0], sizeof(un), stdin);
      un[strlen(un) - 1] = 0;
      username = &un[0];
   }

   /* Verify */
   while (fgets(line, sizeof(line), users_file))
   {
      ptr = strtok(line, ":");
      if (!strcmp(username, ptr))
      {
         printf("Existing user: %s\n", username);
         goto error;
      }

      number_of_users++;
   }

   if (number_of_users > NUMBER_OF_USERS)
   {
      printf("Too many users\n");
      goto error;
   }

   /* Password */
   if (password == NULL)
   {
      printf("Password : ");

      memset(&pwd, 0, sizeof(pwd));
      fgets(&pwd[0], sizeof(pwd), stdin);
      pwd[strlen(pwd) - 1] = 0;
      password = &pwd[0];
   }

   pgagroal_encrypt(password, master_key, &encrypted);
   pgagroal_base64_encode(encrypted, &encoded);

   snprintf(line, sizeof(line), "%s:%s\n", username, encoded);

   fputs(line, users_file);

   free(master_key);
   free(encrypted);
   free(encoded);

   fclose(users_file);

   return 0;

error:

   free(master_key);
   free(encrypted);
   free(encoded);

   if (users_file)
   {
      fclose(users_file);
   }

   return 1;
}

static int
update_user(char* users_path, char* username, char* password)
{
   FILE* users_file = NULL;
   FILE* users_file_tmp = NULL;
   char tmpfilename[MISC_LENGTH];
   char line[MISC_LENGTH];
   char line_copy[MISC_LENGTH];
   char* master_key = NULL;
   char* ptr = NULL;
   char* encrypted = NULL;
   char* encoded = NULL;
   char un[IDENTIFIER_LENGTH];
   char pwd[IDENTIFIER_LENGTH];
   bool found = false;

   memset(&tmpfilename, 0, sizeof(tmpfilename));

   if (pgagroal_get_master_key(&master_key))
   {
      printf("Invalid master key\n");
      goto error;
   }

   users_file = fopen(users_path, "r");
   if (!users_file)
   {
      printf("%s not found\n", users_path);
      goto error;
   }

   snprintf(tmpfilename, sizeof(tmpfilename), "%s.tmp", users_path);
   users_file_tmp = fopen(tmpfilename, "w+");

   /* User */
   if (username == NULL)
   {
      printf("User name: ");
   
      memset(&un, 0, sizeof(un));
      fgets(&un[0], sizeof(un), stdin);
      un[strlen(un) - 1] = 0;
      username = &un[0];
   }

   /* Update */
   while (fgets(line, sizeof(line), users_file))
   {
      memset(&line_copy, 0, sizeof(line_copy));
      memcpy(&line_copy, &line, strlen(line));

      ptr = strtok(line, ":");
      if (!strcmp(username, ptr))
      {
         /* Password */
         if (password == NULL)
         {
            printf("Password : ");

            memset(&pwd, 0, sizeof(pwd));
            fgets(&pwd[0], sizeof(pwd), stdin);
            pwd[strlen(pwd) - 1] = 0;
            password = &pwd[0];
         }

         pgagroal_encrypt(password, master_key, &encrypted);
         pgagroal_base64_encode(encrypted, &encoded);

         memset(&line, 0, sizeof(line));
         snprintf(line, sizeof(line), "%s:%s\n", username, encoded);

         fputs(line, users_file_tmp);

         found = true;
      }
      else
      {
         fputs(line_copy, users_file_tmp);
      }
   }

   if (!found)
   {
      printf("User '%s' not found\n", username);
      goto error;
   }

   free(master_key);
   free(encrypted);
   free(encoded);

   fclose(users_file);
   fclose(users_file_tmp);

   rename(tmpfilename, users_path);

   return 0;

error:

   free(master_key);
   free(encrypted);
   free(encoded);

   if (users_file)
   {
      fclose(users_file);
   }

   if (users_file_tmp)
   {
      fclose(users_file_tmp);
   }

   if (strlen(tmpfilename) > 0)
   {
      remove(tmpfilename);
   }

   return 1;
}

static int
remove_user(char* users_path, char* username)
{
   FILE* users_file = NULL;
   FILE* users_file_tmp = NULL;
   char tmpfilename[MISC_LENGTH];
   char line[MISC_LENGTH];
   char line_copy[MISC_LENGTH];
   char* ptr = NULL;
   char un[IDENTIFIER_LENGTH];
   bool found = false;

   users_file = fopen(users_path, "r");
   if (!users_file)
   {
      printf("%s not found\n", users_path);
      goto error;
   }

   memset(&tmpfilename, 0, sizeof(tmpfilename));
   snprintf(tmpfilename, sizeof(tmpfilename), "%s.tmp", users_path);
   users_file_tmp = fopen(tmpfilename, "w+");

   /* User */
   if (username == NULL)
   {
      printf("User name: ");
   
      memset(&un, 0, sizeof(un));
      fgets(&un[0], sizeof(un), stdin);
      un[strlen(un) - 1] = 0;
      username = &un[0];
   }

   /* Remove */
   while (fgets(line, sizeof(line), users_file))
   {
      memset(&line_copy, 0, sizeof(line_copy));
      memcpy(&line_copy, &line, strlen(line));

      ptr = strtok(line, ":");
      if (!strcmp(username, ptr))
      {
         found = true;
      }
      else
      {
         fputs(line_copy, users_file_tmp);
      }
   }

   if (!found)
   {
      printf("User '%s' not found\n", username);
      goto error;
   }

   fclose(users_file);
   fclose(users_file_tmp);

   rename(tmpfilename, users_path);

   return 0;

error:

   if (users_file)
   {
      fclose(users_file);
   }

   if (users_file_tmp)
   {
      fclose(users_file_tmp);
   }

   if (strlen(tmpfilename) > 0)
   {
      remove(tmpfilename);
   }

   return 1;
}

static int
list_users(char* users_path)
{
   FILE* users_file = NULL;
   char line[MISC_LENGTH];
   char* ptr = NULL;

   users_file = fopen(users_path, "r");
   if (!users_file)
   {
      goto error;
   }

   /* List */
   while (fgets(line, sizeof(line), users_file))
   {
      ptr = strtok(line, ":");
      printf("%s\n", ptr);
   }

   fclose(users_file);

   return 0;

error:

   if (users_file)
   {
      fclose(users_file);
   }

   return 1;
}
