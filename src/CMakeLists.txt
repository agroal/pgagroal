#
# Add files for libpgagroal
#
FILE(GLOB SOURCE_FILES "libpgagroal/*.c")
FILE(GLOB HEADER_FILES "include/*.h")

set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})

#
# OS
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

  add_compile_options(-DHAVE_LINUX)
  add_compile_options(-D_POSIX_C_SOURCE=200809L)

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

  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

else()

  add_compile_options(-D_XOPEN_SOURCE=700)
  add_compile_options(-D_BSD_SOURCE)
  add_compile_options(-D_DEFAULT_SOURCE)
  add_compile_options(-D__BSD_VISIBLE)

  if (${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    add_compile_options(-DHAVE_OPENBSD)
  elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
     add_compile_options(-DHAVE_FREEBSD)
  endif()

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
  )

  #
  # Library directories
  #
  link_libraries(
    ${LIBEV_LIBRARIES}
    ${OPENSSL_LIBRARIES}
  )
endif()

#
# Compile options
#
add_compile_options(-g)
add_compile_options(-Wall)
add_compile_options(-std=c17)
add_compile_options(-D__USE_ISOC11)
add_compile_options(-D_GNU_SOURCE)
add_compile_options(-Wno-deprecated)
add_compile_options(-Wno-deprecated-declarations)

#
# version number and string management
#
add_compile_options(-DPGAGROAL_MAJOR_VERSION=${VERSION_MAJOR})
add_compile_options(-DPGAGROAL_MINOR_VERSION=${VERSION_MINOR})
add_compile_options(-DPGAGROAL_PATCH_VERSION=${VERSION_PATCH})
add_compile_options(-DPGAGROAL_VERSION="${VERSION_STRING}")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wstrict-prototypes)
endif()

check_c_compiler_flag(-fPIC HAS_PIC)
if (HAS_PIC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  include(CheckPIESupported)
  check_pie_supported()
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_compile_options(-O0)
  add_compile_options(-DDEBUG)

  check_c_compiler_flag(-Wformat HAS_FORMAT)
  if (HAS_FORMAT)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat")
  endif()

  check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
  if (HAS_FORMAT_SECURITY)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat-security")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACK_PROTECTOR_STRONG)
  if (HAS_STACK_PROTECTOR_STRONG)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fstack-protector-strong")
  endif()

  check_c_compiler_flag(-rdynamic HAS_DYNAMIC)
  if (HAS_DYNAMIC)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
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

  check_c_compiler_flag(-Wl,-z,relro HAS_RELRO)
  if (HAS_RELRO)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
  endif()

  check_c_compiler_flag(-Wl,-z,now HAS_NOW)
  if (HAS_NOW)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,now")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
  endif()

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
      add_compile_options(-fsanitize=address)

      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address")
    endif()
  endif()

  check_c_compiler_flag(-fno-omit-frame-pointer HAS_NO_OMIT_FRAME_POINTER)
  if (HAS_NO_OMIT_FRAME_POINTER)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer")
  endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)

  check_c_compiler_flag(-Wformat HAS_FORMAT)
  if (HAS_FORMAT)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat")
  endif()

  check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
  if (HAS_FORMAT_SECURITY)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat-security")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACK_PROTECTOR_STRONG)
  if (HAS_STACK_PROTECTOR_STRONG)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fstack-protector-strong")
  endif()

  check_c_compiler_flag(-rdynamic HAS_DYNAMIC)
  if (HAS_DYNAMIC)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
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

  check_c_compiler_flag(-Wl,-z,relro HAS_RELRO)
  if (HAS_RELRO)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
  endif()

  check_c_compiler_flag(-Wl,-z,now HAS_NOW)
  if (HAS_NOW)
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
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal)
else()
  set_target_properties(pgagroal-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal)
endif()
target_link_libraries(pgagroal-bin pgagroal)

install(TARGETS pgagroal-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-cli
#
add_executable(pgagroal-cli-bin cli.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-cli-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal-cli)
else()
  set_target_properties(pgagroal-cli-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal-cli)
endif()
target_link_libraries(pgagroal-cli-bin pgagroal)

install(TARGETS pgagroal-cli-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-admin
#
add_executable(pgagroal-admin-bin admin.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-admin-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal-admin)
else()
  set_target_properties(pgagroal-admin-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal-admin)
endif()
target_link_libraries(pgagroal-admin-bin pgagroal)

install(TARGETS pgagroal-admin-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
