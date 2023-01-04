# - Try to find libatomic
# Once done this will define
#  LIBATOMIC_FOUND   - System has libatomic
#  LIBATOMIC_LIBRARY - The library needed to use libatomic

FIND_LIBRARY(LIBATOMIC_LIBRARY NAMES atomic atomic.so.1 libatomic.so.1
  HINTS
  /usr/local/lib64
  /usr/local/lib
  /opt/local/lib64
  /opt/local/lib
  /usr/lib64
  /usr/lib
  /lib64
  /lib
)

IF (LIBATOMIC_LIBRARY)
  SET(LIBATOMIC_FOUND TRUE)
ELSE ()
  SET(LIBATOMIC_FOUND FALSE)
ENDIF ()
