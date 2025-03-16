#
# check support
#

find_package(PkgConfig)
pkg_check_modules(PC_CHECK check)

find_path(CHECK_INCLUDE_DIR
  NAMES check.h
  PATHS ${PC_CHECK_INCLUDE_DIRS}
  PATH_SUFFIXES check
)

find_library(CHECK_LIBRARY
  NAMES check
  PATHS ${PC_CHECK_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Check DEFAULT_MSG CHECK_INCLUDE_DIR CHECK_LIBRARY)

if (CHECK_FOUND)
  set(CHECK_LIBRARIES ${CHECK_LIBRARY})
  set(CHECK_INCLUDE_DIRS ${CHECK_INCLUDE_DIR})
else ()
  set(check FALSE)
endif ()

mark_as_advanced(CHECK_INCLUDE_DIR CHECK_LIBRARY)
