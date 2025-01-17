#
# ZSTD support
#

find_path(ZSTD_INCLUDE_DIR
  NAMES zstd.h
)
find_library(ZSTD_LIBRARY
  NAMES zstd
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Zstd REQUIRED_VARS
                                  ZSTD_LIBRARY ZSTD_INCLUDE_DIR)

if(ZSTD_FOUND)
  set(ZSTD_LIBRARIES     ${ZSTD_LIBRARY})
  set(ZSTD_INCLUDE_DIRS  ${ZSTD_INCLUDE_DIR})
endif()

mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)
