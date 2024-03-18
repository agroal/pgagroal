/*
 * Copyright (C) 2024 The pgagroal community
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
#include <json.h>
#include <errno.h>

cJSON*
pgagroal_json_create_new_command_object(char* command_name, bool success, char* executable_name, char* executable_version)
{
   // root of the JSON structure
   cJSON* json = cJSON_CreateObject();

   if (!json)
   {
      goto error;
   }

   // the command structure
   cJSON* command = cJSON_CreateObject();
   if (!command)
   {
      goto error;
   }

   // insert meta-data about the command
   cJSON_AddStringToObject(command, JSON_TAG_COMMAND_NAME, command_name);
   cJSON_AddStringToObject(command, JSON_TAG_COMMAND_STATUS, success ? JSON_STRING_SUCCESS : JSON_STRING_ERROR);
   cJSON_AddNumberToObject(command, JSON_TAG_COMMAND_ERROR, success ? JSON_BOOL_SUCCESS : JSON_BOOL_ERROR);
   cJSON_AddNumberToObject(command, JSON_TAG_COMMAND_EXIT_STATUS, success ? 0 : EXIT_STATUS_DATA_ERROR);

   // the output of the command, this has to be filled by the caller
   cJSON* output = cJSON_CreateObject();
   if (!output)
   {
      goto error;
   }

   cJSON_AddItemToObject(command, JSON_TAG_COMMAND_OUTPUT, output);

   // who has launched the command ?
   cJSON* application = cJSON_CreateObject();
   if (!application)
   {
      goto error;
   }

   long minor = strtol(&executable_version[2], NULL, 10);
   if (errno == ERANGE || minor <= LONG_MIN || minor >= LONG_MAX)
   {
      goto error;
   }
   long patch = strtol(&executable_version[5], NULL, 10);
   if (errno == ERANGE || patch <= LONG_MIN || patch >= LONG_MAX)
   {
      goto error;
   }

   cJSON_AddStringToObject(application, JSON_TAG_APPLICATION_NAME, executable_name);
   cJSON_AddNumberToObject(application, JSON_TAG_APPLICATION_VERSION_MAJOR, executable_version[0] - '0');
   cJSON_AddNumberToObject(application, JSON_TAG_APPLICATION_VERSION_MINOR, (int)minor);
   cJSON_AddNumberToObject(application, JSON_TAG_APPLICATION_VERSION_PATCH, (int)patch);
   cJSON_AddStringToObject(application, JSON_TAG_APPLICATION_VERSION, executable_version);

   // add objects to the whole json thing
   cJSON_AddItemToObject(json, "command", command);
   cJSON_AddItemToObject(json, "application", application);

   return json;

error:
   if (json)
   {
      cJSON_Delete(json);
   }

   return NULL;

}

cJSON*
pgagroal_json_extract_command_output_object(cJSON* json)
{
   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   return cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_OUTPUT);

error:
   return NULL;

}

bool
pgagroal_json_is_command_name_equals_to(cJSON* json, char* command_name)
{
   if (!json || !command_name || strlen(command_name) <= 0)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* cName = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_NAME);
   if (!cName || !cJSON_IsString(cName) || !cName->valuestring)
   {
      goto error;
   }

   return !strncmp(command_name,
                   cName->valuestring,
                   MISC_LENGTH);

error:
   return false;
}

int
pgagroal_json_set_command_object_faulty(cJSON* json, char* message, int exit_status)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* current = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_STATUS);
   if (!current)
   {
      goto error;
   }

   cJSON_SetValuestring(current, message);

   current = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_ERROR);
   if (!current)
   {
      goto error;
   }

   cJSON_SetIntValue(current, JSON_BOOL_ERROR);   // cannot use cJSON_SetBoolValue unless cJSON >= 1.7.16

   current = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_EXIT_STATUS);
   if (!current)
   {
      goto error;
   }

   cJSON_SetIntValue(current, exit_status);

   return 0;

error:
   return 1;

}

bool
pgagroal_json_is_command_object_faulty(cJSON* json)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* status = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_ERROR);
   if (!status || !cJSON_IsNumber(status))
   {
      goto error;
   }

   return status->valueint == JSON_BOOL_SUCCESS ? false : true;

error:
   return false;

}

int
pgagroal_json_command_object_exit_status(cJSON* json)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* status = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_EXIT_STATUS);
   if (!status || !cJSON_IsNumber(status))
   {
      goto error;
   }

   return status->valueint;

error:
   return EXIT_STATUS_DATA_ERROR;
}

const char*
pgagroal_json_get_command_object_status(cJSON* json)
{
   if (!json)
   {
      goto error;
   }

   cJSON* command = cJSON_GetObjectItemCaseSensitive(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   cJSON* status = cJSON_GetObjectItemCaseSensitive(command, JSON_TAG_COMMAND_STATUS);
   if (!cJSON_IsString(status) || (status->valuestring == NULL))
   {
      goto error;
   }

   return status->valuestring;
error:
   return NULL;

}

int
pgagroal_json_print_and_free_json_object(cJSON* json)
{
   int status = pgagroal_json_command_object_exit_status(json);
   printf("%s\n", cJSON_Print(json));
   cJSON_Delete(json);
   return status;
}
