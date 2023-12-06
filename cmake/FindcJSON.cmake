# FindcJSON.cmake
# Tries to find cJSON libraries on the system
# (e.g., on Rocky Linux: cjson and cjson-devel)
#
# Inspired by <https://sources.debian.org/src/monado/21.0.0~dfsg1-1/cmake/FindcJSON.cmake/>
#
# If cJSON is found, sets the following variables:
# - CJSON_INCLUDE_DIRS
# - CJSON_LIBRARIES
# - CJSON_VERSION
#
# In the header file cJSON.h the library version is specified as:
# #define CJSON_VERSION_MAJOR 1
# #define CJSON_VERSION_MINOR 7
# #define CJSON_VERSION_PATCH 14


find_path(
    CJSON_INCLUDE_DIR
    NAMES cjson/cJSON.h
    PATH_SUFFIXES include)
find_library(
    CJSON_LIBRARY
    NAMES cjson
    PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cJSON REQUIRED_VARS CJSON_INCLUDE_DIR
                                                      CJSON_LIBRARY)
if(CJSON_FOUND)
  # these variables are needed for the build
  set( CJSON_INCLUDE_DIRS "${CJSON_INCLUDE_DIR}" )
  set( CJSON_LIBRARIES    "${CJSON_LIBRARY}"     )

  # try to get out the library version from the headers
  file(STRINGS "${CJSON_INCLUDE_DIR}/cjson/cJSON.h"
    CJSON_VERSION_MAJOR REGEX "^#define[ \t]+CJSON_VERSION_MAJOR[ \t]+[0-9]+")
  file(STRINGS "${CJSON_INCLUDE_DIR}/cjson/cJSON.h"
    CJSON_VERSION_MINOR REGEX "^#define[ \t]+CJSON_VERSION_MINOR[ \t]+[0-9]+")
    file(STRINGS "${CJSON_INCLUDE_DIR}/cjson/cJSON.h"
    CJSON_VERSION_PATCH REGEX "^#define[ \t]+CJSON_VERSION_PATCH[ \t]+[0-9]+")
  string(REGEX REPLACE "[^0-9]+" "" CJSON_VERSION_MAJOR "${CJSON_VERSION_MAJOR}")
  string(REGEX REPLACE "[^0-9]+" "" CJSON_VERSION_MINOR "${CJSON_VERSION_MINOR}")
  string(REGEX REPLACE "[^0-9]+" "" CJSON_VERSION_PATCH "${CJSON_VERSION_PATCH}")
  set(CJSON_VERSION "${CJSON_VERSION_MAJOR}.${CJSON_VERSION_MINOR}.${CJSON_VERSION_PATCH}")
  unset(CJSON_VERSION_MINOR)
  unset(CJSON_VERSION_MAJOR)
  unset(CJSON_VERSION_PATCH)
endif()

mark_as_advanced( CJSON_INCLUDE_DIR CJSON_LIBRARY )  
