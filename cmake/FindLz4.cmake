#
# LZ4 Support
#
  
find_path(LZ4_INCLUDE_DIR 
  NAMES lz4.h
)
find_library(LZ4_LIBRARY 
  NAMES lz4
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lz4 DEFAULT_MSG
                                  LZ4_LIBRARY LZ4_INCLUDE_DIR)

if(LZ4_FOUND)
  set(LZ4_LIBRARIES     ${LZ4_LIBRARY})
  set(LZ4_INCLUDE_DIRS  ${LZ4_INCLUDE_DIR})
endif()

mark_as_advanced(LZ4_INCLUDE_DIR LZ4_LIBRARY)
