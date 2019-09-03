#
# Add files for libpgagroal
#
FILE(GLOB SOURCE_FILES "libpgagroal/*.c")
FILE(GLOB HEADER_FILES "include/*.h")

set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LIBEV_INCLUDE_DIRS}
)

link_libraries(
  ${LIBEV_LIBRARIES}
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -std=c17 -D_POSIX_C_SOURCE=200809L -D__USE_ISOC11 -D_GNU_SOURCE")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -march=native -mtune=native -flto")

#
# Build libpgagroal
#
add_library(pgagroal SHARED ${SOURCES})
set_target_properties(pgagroal PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(pgagroal PUBLIC)

install(TARGETS pgagroal DESTINATION ${CMAKE_INSTALL_LIBDIR}/)

#
# Build pgagroal
#
add_executable(pgagroal-bin main.c ${RESOURCE_OBJECT})
set_target_properties(pgagroal-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgagroal)
target_link_libraries(pgagroal-bin pgagroal)

install(TARGETS pgagroal DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-cli
#
add_executable(pgagroal-cli-bin cli.c ${RESOURCE_OBJECT})
set_target_properties(pgagroal-cli-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgagroal-cli)
target_link_libraries(pgagroal-cli-bin pgagroal)

install(TARGETS pgagroal-cli-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
