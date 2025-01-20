# - Try to find liburing
# Once done this will define
#  LIBURING_FOUND   - System has liburing
#  LIBURING_LIBRARY - The library needed to use liburing

FIND_LIBRARY(LIBURING_LIBRARY NAMES liburing liburing.a liburing.so liburing.so.2
   HINTS
   /usr/lib64
   /usr/lib
   /lib64
   /lib
)

IF (LIBURING_LIBRARY)
   SET(LIBURING_FOUND TRUE)
ELSE ()
   SET(LIBURING_FOUND FALSE)
ENDIF ()
