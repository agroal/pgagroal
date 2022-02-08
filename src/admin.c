/*
 * Copyright (C) 2021 Red Hat
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
#include <logging.h>
#include <security.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>

#define DEFAULT_PASSWORD_LENGTH 64

#define ACTION_UNKNOWN     0
#define ACTION_MASTER_KEY  1
#define ACTION_ADD_USER    2
#define ACTION_UPDATE_USER 3
#define ACTION_REMOVE_USER 4
#define ACTION_LIST_USERS  5

static char CHARS[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                       'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                       '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                       '!', '@', '#', '$', '%', '^', '&',  '*', '(', ')', '-', '_', '=', '+', '[', '{', ']', '}', '\\', '|', ';', ':',
                       '\'', '\"', ',', '<', '.',  '>', '/', '?'};

static int master_key(char* password, bool generate_pwd, int pwd_length);
static bool is_valid_key(char* key);
static int add_user(char* users_path, char* username, char* password, bool generate_pwd, int pwd_length);
static int update_user(char* users_path, char* username, char* password, bool generate_pwd, int pwd_length);
static int remove_user(char* users_path, char* username);
static int list_users(char* users_path);
static char* generate_password(int pwd_length);

/**
 * Function to catch the SIGINT
 * when editing the Master Key
 */
void master_key_sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf( "\nAborting...\n" );
    char buf[MISC_LENGTH];
    snprintf(&buf[0], sizeof(buf), "%s/.pgagroal/master.key", pgagroal_get_home_directory());
    chmod(&buf[0], S_IRUSR | S_IWUSR);
    exit( 1 );
  }
}


static void
version(void)
{
   printf("pgagroal-admin %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal-admin %s\n", VERSION);
   printf("  Administration utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-admin [ -f FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -f, --file FILE         Set the path to a user file\n");
   printf("  -U, --user USER         Set the user name\n");
   printf("  -P, --password PASSWORD Set the password for the user\n");
   printf("  -g, --generate          Generate a password\n");
   printf("  -l, --length            Password length\n");
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
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char **argv)
{
   int exit_code = 0;
   int c;
   char* username = NULL;
   char* password = NULL;
   char* file_path = NULL;
   bool generate_pwd = false;
   int pwd_length = DEFAULT_PASSWORD_LENGTH;
   int option_index = 0;
   int32_t action = ACTION_UNKNOWN;

   while (1)
   {
      static struct option long_options[] =
      {
         {"user",  required_argument, 0, 'U'},
         {"password",  required_argument, 0, 'P'},
         {"file",  required_argument, 0, 'f'},
         {"generate",  no_argument, 0, 'g'},
         {"length",  required_argument, 0, 'l'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "gV?f:U:P:l:",
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
         case 'f':
            file_path = optarg;
            break;
         case 'g':
            generate_pwd = true;
            break;
         case 'l':
            pwd_length = atoi(optarg);
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

   if (getuid() == 0)
   {
      printf("pgagroal: Using the root account is not allowed\n");
      exit(1);
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
	// turn on signal handler
	signal( SIGINT, master_key_sig_handler );
	
         if (master_key(password, generate_pwd, pwd_length))
         {
            printf("Error for master key\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_ADD_USER)
      {
         if (file_path != NULL)
         {
            if (add_user(file_path, username, password, generate_pwd, pwd_length))
            {
               printf("Error for add-user\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing file argument\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_UPDATE_USER)
      {
         if (file_path != NULL)
         {
            if (update_user(file_path, username, password, generate_pwd, pwd_length))
            {
               printf("Error for update-user\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing file argument\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_REMOVE_USER)
      {
         if (file_path != NULL)
         {
            if (remove_user(file_path, username))
            {
               printf("Error for remove-user\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing file argument\n");
            exit_code = 1;
         }
      }
      else if (action == ACTION_LIST_USERS)
      {
         if (file_path != NULL)
         {
            if (list_users(file_path))
            {
               printf("Error for list-users\n");
               exit_code = 1;
            }
         }
         else
         {
            printf("Missing file argument\n");
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
master_key(char* password, bool generate_pwd, int pwd_length)
{
   FILE* file = NULL;
   char buf[MISC_LENGTH];
   char* encoded = NULL;
   struct stat st = {0};
   bool do_free = true;

   if (pgagroal_get_home_directory() == NULL)
   {
      char* username = pgagroal_get_user_name();

      if (username != NULL)
      {
         printf("No home directory for user \'%s\'\n", username);
      }
      else
      {
         printf("No home directory for user running pgagroal\n");
      }

      goto error;
   }

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
         printf("Wrong permissions for ~/.pgagroal (must be 0700)\n");
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
         printf("Wrong permissions for ~/.pgagroal/master.key (must be 0600)\n");
         goto error;
      }
   }


   file = fopen(&buf[0], "w+");
   if (file == NULL)
   {
      printf("Could not write to master key file '%s' due to %s\n", &buf[0], strerror(errno));
      goto error;
   }

   if (password == NULL)
   {
     int loops = 0; /* keep track about how many times the user has tried */
     
      if (!generate_pwd)
      {
         while (!is_valid_key(password))
         {
            if (password != NULL)
            {
               free(password);
               password = NULL;
            }

	    // if the user has tried with an invalid
	    // password, warn her!
	    if ( loops > 0 )
	      printf( "Invalid master key, try again\n" );
	    
            printf("Master key (will not echo): ");
            password = pgagroal_get_password();
            printf("\n");
	    loops++;
         }
      }
      else
      {
         password = generate_password(pwd_length);
         do_free = false;
      }
   }
   else
   {
      do_free = false;

      if (!is_valid_key(password))
      {
         goto error;
      }
   }

   pgagroal_base64_encode(password, strlen(password), &encoded);
   fputs(encoded, file);
   free(encoded);

   if (do_free)
   {
      free(password);
   }

   fclose(file);

   chmod(&buf[0], S_IRUSR | S_IWUSR);
   printf( "Master Key stored into [%s]\n", &buf[0] );

   return 0;

error:

   free(encoded);

   if (do_free)
   {
      free(password);
   }

   if (file)
   {
      fclose(file);
   }

   return 1;
}

static bool
is_valid_key(char* key)
{
   char c;
   
   if (!key)
   {
      return false;
   }

   if (strlen(key) < 8)
   {
      return false;
   }

   for (int i = 0; i < strlen(key); i++)
   {
      c = *(key + i);

      /* Only support ASCII for now */
      if ((unsigned char)c & 0x80)
      {
         return false;
      }
   }

   return true;
}

static int
add_user(char* users_path, char* username, char* password, bool generate_pwd, int pwd_length)
{
   FILE* users_file = NULL;
   char line[MISC_LENGTH];
   char* master_key = NULL;
   char* ptr = NULL;
   char* encrypted = NULL;
   int encrypted_length = 0;
   char* encoded = NULL;
   char un[MAX_USERNAME_LENGTH];
   int number_of_users = 0;
   bool do_verify = true;
   char* verify = NULL;
   bool do_free = true;

   if (pgagroal_get_master_key(&master_key))
   {
      printf("Invalid master key\n");
      goto error;
   }

   if (password != NULL)
   {
      do_verify = false;
      do_free = false;
   }

   users_file = fopen(users_path, "a+");
   if (users_file == NULL)
   {
      printf("Could not append to users file '%s' due to %s\n", users_path, strerror(errno));
      goto error;
   }

   /* User */
   if (username == NULL)
   {
username:
      printf("User name: ");
   
      memset(&un, 0, sizeof(un));
      if (fgets(&un[0], sizeof(un), stdin) == NULL)
      {
         goto error;
      }
      un[strlen(un) - 1] = 0;
      username = &un[0];
   }

   if (username == NULL || strlen(username) == 0)
   {
      goto username;
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
password:
      if (generate_pwd)
      {
         password = generate_password(pwd_length);
         do_verify = false;
         printf("Password : %s", password);
      }
      else
      {
         printf("Password : ");

         if (password != NULL)
         {
            free(password);
            password = NULL;
         }

         password = pgagroal_get_password();
      }
      printf("\n");
   }

   for (int i = 0; i < strlen(password); i++)
   {
      if ((unsigned char)(*(password + i)) & 0x80)
      {
         goto password;
      }
   }

   if (do_verify)
   {
      printf("Verify   : ");

      if (verify != NULL)
      {
         free(verify);
         verify = NULL;
      }

      verify = pgagroal_get_password();
      printf("\n");

      if (strlen(password) != strlen(verify) || memcmp(password, verify, strlen(password)) != 0)
      {
         goto password;
      }
   }

   pgagroal_encrypt(password, master_key, &encrypted, &encrypted_length);
   pgagroal_base64_encode(encrypted, encrypted_length, &encoded);

   snprintf(line, sizeof(line), "%s:%s\n", username, encoded);

   fputs(line, users_file);

   free(master_key);
   free(encrypted);
   free(encoded);
   if (do_free)
   {
      free(password);
   }
   free(verify);

   fclose(users_file);

   return 0;

error:

   free(master_key);
   free(encrypted);
   free(encoded);
   if (do_free)
   {
      free(password);
   }
   free(verify);

   if (users_file)
   {
      fclose(users_file);
   }

   return 1;
}

static int
update_user(char* users_path, char* username, char* password, bool generate_pwd, int pwd_length)
{
   FILE* users_file = NULL;
   FILE* users_file_tmp = NULL;
   char tmpfilename[MISC_LENGTH];
   char line[MISC_LENGTH];
   char line_copy[MISC_LENGTH];
   char* master_key = NULL;
   char* ptr = NULL;
   char* encrypted = NULL;
   int encrypted_length = 0;
   char* encoded = NULL;
   char un[MAX_USERNAME_LENGTH];
   bool found = false;
   bool do_verify = true;
   char* verify = NULL;
   bool do_free = true;

   memset(&tmpfilename, 0, sizeof(tmpfilename));

   if (pgagroal_get_master_key(&master_key))
   {
      printf("Invalid master key\n");
      goto error;
   }

   if (password != NULL)
   {
      do_verify = false;
      do_free = false;
   }

   users_file = fopen(users_path, "r");
   if (!users_file)
   {
      printf("%s not found\n", users_path);
      goto error;
   }

   snprintf(tmpfilename, sizeof(tmpfilename), "%s.tmp", users_path);
   users_file_tmp = fopen(tmpfilename, "w+");
   if (users_file_tmp == NULL)
   {
      printf("Could not write to temporary user file '%s' due to %s\n", tmpfilename, strerror(errno));
      goto error;
   }

   /* User */
   if (username == NULL)
   {
username:
      printf("User name: ");
   
      memset(&un, 0, sizeof(un));
      if (fgets(&un[0], sizeof(un), stdin) == NULL)
      {
         goto error;
      }
      un[strlen(un) - 1] = 0;
      username = &un[0];
   }

   if (username == NULL || strlen(username) == 0)
   {
      goto username;
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
password:
            if (generate_pwd)
            {
               password = generate_password(pwd_length);
               do_verify = false;
               printf("Password : %s", password);
            }
            else
            {
               printf("Password : ");

               if (password != NULL)
               {
                  free(password);
                  password = NULL;
               }

               password = pgagroal_get_password();
            }
            printf("\n");
         }

         for (int i = 0; i < strlen(password); i++)
         {
            if ((unsigned char)(*(password + i)) & 0x80)
            {
               goto password;
            }
         }

         if (do_verify)
         {
            printf("Verify   : ");

            if (verify != NULL)
            {
               free(verify);
               verify = NULL;
            }

            verify = pgagroal_get_password();
            printf("\n");

            if (strlen(password) != strlen(verify) || memcmp(password, verify, strlen(password)) != 0)
            {
               goto password;
            }
         }

         pgagroal_encrypt(password, master_key, &encrypted, &encrypted_length);
         pgagroal_base64_encode(encrypted, encrypted_length, &encoded);

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
   if (do_free)
   {
      free(password);
   }
   free(verify);

   fclose(users_file);
   fclose(users_file_tmp);

   rename(tmpfilename, users_path);

   return 0;

error:

   free(master_key);
   free(encrypted);
   free(encoded);
   if (do_free)
   {
      free(password);
   }
   free(verify);

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
   char un[MAX_USERNAME_LENGTH];
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
   if (users_file_tmp == NULL)
   {
      printf("Could not write to temporary user file '%s' due to %s\n", tmpfilename, strerror(errno));
      goto error;
   }

   /* User */
   if (username == NULL)
   {
username:
      printf("User name: ");
   
      memset(&un, 0, sizeof(un));
      if (fgets(&un[0], sizeof(un), stdin) == NULL)
      {
         goto error;
      }
      un[strlen(un) - 1] = 0;
      username = &un[0];
   }

   if (username == NULL || strlen(username) == 0)
   {
      goto username;
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

static char*
generate_password(int pwd_length)
{
   char* pwd;
   size_t s;
   time_t t;

   s = pwd_length + 1;

   pwd = malloc(s);
   memset(pwd, 0, s);

   srand((unsigned)time(&t));

   for (int i = 0; i < s; i++)
   {
      *((char*)(pwd + i)) = CHARS[rand() % sizeof(CHARS)];
   }
   *((char*)(pwd + pwd_length)) = '\0';

   return pwd;
}
