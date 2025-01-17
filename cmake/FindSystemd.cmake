# - Try to find systemd
# Once done this will define
#  SYSTEMD_FOUND        - System has systemd
#  SYSTEMD_INCLUDE_DIRS - The systemd include directories
#  SYSTEMD_LIBRARIES    - The libraries needed to use systemd

find_path(SYSTEMD_INCLUDE_DIR
  NAMES systemd/sd-daemon.h
)
find_library(SYSTEMD_LIBRARY
  NAMES systemd
)

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set SYSTEMD_FOUND to TRUE
# if all listed variables are TRUE and the requested version matches.
find_package_handle_standard_args(Systemd REQUIRED_VARS
                                  SYSTEMD_LIBRARY SYSTEMD_INCLUDE_DIR
                                  VERSION_VAR SYSTEMD_VERSION)

if(SYSTEMD_FOUND)
  set(SYSTEMD_LIBRARIES     ${SYSTEMD_LIBRARY})
  set(SYSTEMD_INCLUDE_DIRS  ${SYSTEMD_INCLUDE_DIR})
endif()

mark_as_advanced(SYSTEMD_INCLUDE_DIR SYSTEMD_LIBRARY)
