#
# Add files for libpgagroal
#
FILE(GLOB SOURCE_FILES "libpgagroal/*.c")
FILE(GLOB HEADER_FILES "include/*.h")

set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})

#
# Include directories
#
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LIBEV_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIRS}
  ${SYSTEMD_INCLUDE_DIRS}
)

#
# Library directories
#
link_libraries(
  ${LIBEV_LIBRARIES}
  ${OPENSSL_LIBRARIES}
  ${SYSTEMD_LIBRARIES}
)

#
# Compile options
#
add_compile_options(-g)
add_compile_options(-Wall)
add_compile_options(-std=c17)
add_compile_options(-D_POSIX_C_SOURCE=200809L)
add_compile_options(-D__USE_ISOC11)
add_compile_options(-D_GNU_SOURCE)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wformat -Wformat-security -fno-omit-frame-pointer -fstack-protector-strong -DDEBUG")

if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)

  check_c_compiler_flag(-Wformat HAS_FORMAT)
  if (HAS_FORMAT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat")
  endif()

  check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
  if (HAS_FORMAT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-security")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACKPROTECTOR_STRONG)
  if (HAS_STACKPROTECTOR_STRONG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
  else()
    check_c_compiler_flag(-fstack-protector HAS_STACKPROTECTOR)
    if (HAS_STACKPROTECTOR)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
    endif()
  endif()

  check_c_compiler_flag(-fPIC HAS_PIC)
  if (HAS_PIC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
  endif()

  check_c_compiler_flag(-fPIE HAS_PIE)
  if (HAS_PIE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pie")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie")
  endif()

  check_c_compiler_flag(-Wl,-z,relro HAS_RELRO)
  if (HAS_RELRO)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
  endif()

  check_c_compiler_flag(-Wl,-z,now HAS_NOW)
  if (HAS_RELRO)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,now")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
  endif()
endif (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)

if (CMAKE_BUILD_TYPE MATCHES Performance)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)

  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-march=native)
    add_compile_options(-mtune=native)
    add_compile_options(-flto)
  endif()
endif (CMAKE_BUILD_TYPE MATCHES Performance)

#
# Build libpgagroal
#
add_library(pgagroal SHARED ${SOURCES})
set_target_properties(pgagroal PROPERTIES LINKER_LANGUAGE C VERSION ${VERSION_STRING}
                               SOVERSION ${VERSION_MAJOR})
target_link_libraries(pgagroal PUBLIC)

install(TARGETS pgagroal DESTINATION ${CMAKE_INSTALL_LIBDIR}/)

#
# Build pgagroal
#
add_executable(pgagroal-bin main.c ${RESOURCE_OBJECT})
set_target_properties(pgagroal-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgagroal)
target_link_libraries(pgagroal-bin pgagroal)

install(TARGETS pgagroal-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-cli
#
add_executable(pgagroal-cli-bin cli.c ${RESOURCE_OBJECT})
set_target_properties(pgagroal-cli-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgagroal-cli)
target_link_libraries(pgagroal-cli-bin pgagroal)

install(TARGETS pgagroal-cli-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-admin
#
add_executable(pgagroal-admin-bin admin.c ${RESOURCE_OBJECT})
set_target_properties(pgagroal-admin-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgagroal-admin)
target_link_libraries(pgagroal-admin-bin pgagroal)

install(TARGETS pgagroal-admin-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
