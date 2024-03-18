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

#include <cjson/cJSON.h>

/**
 * JSON related command tags, used to build and retrieve
 * a JSON piece of information related to a single command
 */
#define JSON_TAG_COMMAND "command"
#define JSON_TAG_COMMAND_NAME "name"
#define JSON_TAG_COMMAND_STATUS "status"
#define JSON_TAG_COMMAND_ERROR "error"
#define JSON_TAG_COMMAND_OUTPUT "output"
#define JSON_TAG_COMMAND_EXIT_STATUS "exit-status"

#define JSON_TAG_APPLICATION_NAME "name"
#define JSON_TAG_APPLICATION_VERSION_MAJOR "major"
#define JSON_TAG_APPLICATION_VERSION_MINOR "minor"
#define JSON_TAG_APPLICATION_VERSION_PATCH "patch"
#define JSON_TAG_APPLICATION_VERSION "version"

#define JSON_TAG_ARRAY_NAME "list"

/**
 * JSON pre-defined values
 */
#define JSON_STRING_SUCCESS "OK"
#define JSON_STRING_ERROR   "KO"
#define JSON_BOOL_SUCCESS   0
#define JSON_BOOL_ERROR     1

/**
 * Utility method to create a new JSON object that wraps a
 * single command. This method should be called to initialize the
 * object and then the other specific methods that read the
 * answer from pgagroal should populate the object accordingly.
 *
 * Moreover, an 'application' object is placed to indicate from
 * where the command has been launched (i.e., which executable)
 * and at which version.
 *
 * @param command_name the name of the command this object wraps
 * an answer for
 * @param success true if the command is supposed to be succesfull
 * @returns the new JSON object to use and populate
 * @param executable_name the name of the executable that is creating this
 * response object
 */
cJSON*
pgagroal_json_create_new_command_object(char* command_name, bool success, char* executable_name, char* executable_version);

/**
 * Utility method to "jump" to the output JSON object wrapped into
 * a command object.
 *
 * The "output" object is the one that every single method that reads
 * back an answer from pgagroal has to populate in a specific
 * way according to the data received from pgagroal.
 *
 * @param json the command object that wraps the command
 * @returns the pointer to the output object of NULL in case of an error
 */
cJSON*
pgagroal_json_extract_command_output_object(cJSON* json);

/**
 * Utility function to set a command JSON object as faulty, that
 * means setting the 'error' and 'status' message accordingly.
 *
 * @param json the whole json object that must include the 'command'
 * tag
 * @param message the message to use to set the faulty diagnostic
 * indication
 *
 * @param exit status
 *
 * @returns 0 on success
 *
 * Example:
 * json_set_command_object_faulty( json, strerror( errno ) );
 */
int
pgagroal_json_set_command_object_faulty(cJSON* json, char* message, int exit_status);

/**
 * Utility method to inspect if a JSON object that wraps a command
 * is faulty, that means if it has the error flag set to true.
 *
 * @param json the json object to analyzer
 * @returns the value of the error flag in the object, or false if
 * the object is not valid
 */
bool
pgagroal_json_is_command_object_faulty(cJSON* json);

/**
 * Utility method to extract the message related to the status
 * of the command wrapped in the JSON object.
 *
 * @param json the JSON object to analyze
 * #returns the status message or NULL in case the JSON object is not valid
 */
const char*
pgagroal_json_get_command_object_status(cJSON* json);

/**
 * Utility method to check if a JSON object wraps a specific command name.
 *
 * @param json the JSON object to analyze
 * @param command_name the name to search for
 * @returns true if the command name matches, false otherwise and in case
 * the JSON object is not valid or the command name is not valid
 */
bool
pgagroal_json_is_command_name_equals_to(cJSON* json, char* command_name);

/**
 * Utility method to print out the JSON object
 * on standard output.
 *
 * After the object has been printed, it is destroyed, so
 * calling this method will make the pointer invalid
 * and the jeon object cannot be used anymore.
 *
 * This should be the last method to be called
 * when there is the need to print out the information
 * contained in a json object.
 *
 * Since the JSON object will be invalidated, the method
 * returns the status of the JSON command within it
 * to be used.
 *
 * @param json the json object to print
 * @return the command status within the JSON object
 */
int
pgagroal_json_print_and_free_json_object(cJSON* json);

/**
 * Utility function to get the exit status of a given command wrapped in a JSON object.
 *
 * @param json the json object
 * @returns the exit status of the command
 */
int
pgagroal_json_command_object_exit_status(cJSON* json);
