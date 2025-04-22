# man target
add_custom_target(man ALL)

# man page definitions
set(PGAGROAL_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal.1.rst")
set(PGAGROAL_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal.1")
set(PGAGROAL_CLI_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal-cli.1.rst")
set(PGAGROAL_CLI_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal-cli.1")
set(PGAGROAL_ADMIN_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal-admin.1.rst")
set(PGAGROAL_ADMIN_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal-admin.1")
set(PGAGROAL_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal.conf.5.rst")
set(PGAGROAL_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal.conf.5")
set(PGAGROAL_HBA_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal_hba.conf.5.rst")
set(PGAGROAL_HBA_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal_hba.conf.5")
set(PGAGROAL_DATABASES_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal_databases.conf.5.rst")
set(PGAGROAL_DATABASES_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal_databases.conf.5")

# pgagroal.1
add_custom_command(
  OUTPUT ${PGAGROAL_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_SRC_FILE} ${PGAGROAL_DST_FILE}
  DEPENDS ${PGAGROAL_SRC_FILE}
  COMMENT "Generating man page: pgagroal.1"
)

# pgagroal-cli.1
add_custom_command(
  OUTPUT ${PGAGROAL_CLI_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_CLI_SRC_FILE} ${PGAGROAL_CLI_DST_FILE}
  DEPENDS ${PGAGROAL_CLI_SRC_FILE}
  COMMENT "Generating man page: pgagroal-cli.1"
)

# pgagroal-admin.1
add_custom_command(
  OUTPUT ${PGAGROAL_ADMIN_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_ADMIN_SRC_FILE} ${PGAGROAL_ADMIN_DST_FILE}
  DEPENDS ${PGAGROAL_ADMIN_SRC_FILE}
  COMMENT "Generating man page: pgagroal-admin.1"
)

# pgagroal.conf.5
add_custom_command(
  OUTPUT ${PGAGROAL_CONF_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_CONF_SRC_FILE} ${PGAGROAL_CONF_DST_FILE}
  DEPENDS ${PGAGROAL_CONF_SRC_FILE}
  COMMENT "Generating man page: pgagroal.conf.5"
)

# pgagroal_hba.conf.5
add_custom_command(
  OUTPUT ${PGAGROAL_HBA_CONF_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_HBA_CONF_SRC_FILE} ${PGAGROAL_HBA_CONF_DST_FILE}
  DEPENDS ${PGAGROAL_HBA_CONF_SRC_FILE}
  COMMENT "Generating man page: pgagroal_hba.conf.5"
)

# pgagroal_databases.conf.5
add_custom_command(
  OUTPUT ${PGAGROAL_DATABASES_CONF_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_DATABASES_CONF_SRC_FILE} ${PGAGROAL_DATABASES_CONF_DST_FILE}
  DEPENDS ${PGAGROAL_DATABASES_CONF_SRC_FILE}
  COMMENT "Generating man page: pgagroal_databases.conf.5"
)

# Group man page outputs into a target
add_custom_target(manpages ALL
  DEPENDS
    ${PGAGROAL_DST_FILE}
    ${PGAGROAL_CLI_DST_FILE}
    ${PGAGROAL_ADMIN_DST_FILE}
    ${PGAGROAL_CONF_DST_FILE}
    ${PGAGROAL_HBA_CONF_DST_FILE}
    ${PGAGROAL_DATABASES_CONF_DST_FILE}
)

add_dependencies(man manpages)

#
# Install configuration and documentation
#
install(FILES etc/pgagroal.conf DESTINATION share/doc/pgagroal/etc)
install(FILES etc/pgagroal_hba.conf DESTINATION share/doc/pgagroal/etc)

install(DIRECTORY . DESTINATION share/doc/pgagroal FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "images" EXCLUDE PATTERN "man" EXCLUDE PATTERN "manual" EXCLUDE)
install(DIRECTORY images/ DESTINATION share/doc/pgagroal/images FILES_MATCHING PATTERN "*.png")

install(FILES ${PGAGROAL_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGAGROAL_CLI_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGAGROAL_ADMIN_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGAGROAL_CONF_DST_FILE} DESTINATION share/man/man5)
install(FILES ${PGAGROAL_HBA_CONF_DST_FILE} DESTINATION share/man/man5)
install(FILES ${PGAGROAL_DATABASES_CONF_DST_FILE} DESTINATION share/man/man5)

#
# Generate manual
#
if(generation)
  # Files
  FILE(GLOB PREDS "manual/0?-*.md")
  FILE(GLOB POSTDS "manual/9?-*.md")
  FILE(GLOB TDS "tutorial/??_*.md")
  FILE(GLOB UMDS_PRE "manual/user-0?-*.md")
  FILE(GLOB UMDS_POST "manual/user-1?-*.md")
  FILE(GLOB DMDS "manual/dev-??-*.md")
  FILE(GLOB AG "manual/advanced/??-*.md")

  # Manuals definitions
  set(MANUAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/doc")
  get_filename_component(PARENT_DIR ${CMAKE_BINARY_DIR} DIRECTORY)
  set(IMAGE_DIR "${PARENT_DIR}/doc/images")

  add_custom_target(
    copy_images
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMAGE_DIR} ${CMAKE_BINARY_DIR}/images
    COMMENT "Copy images"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.pdf --from markdown --template eisvogel --listings -N --toc ${PREDS} ${UMDS_PRE} ${TDS} ${UMDS_POST} ${POSTDS}
    DEPENDS ${UMDS}
    COMMENT "Generating User Guide PDF documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.pdf --from markdown --template eisvogel --listings -N --toc ${PREDS} ${DMDS} ${POSTDS}
    DEPENDS ${DMDS}
    COMMENT "Generating Developer Guide PDF documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.html -s -f markdown-smart -N --toc -t html5 ${PREDS} ${UMDS_PRE} ${TDS} ${UMDS_POST} ${POSTDS}
    DEPENDS ${UMDS}
    COMMENT "Generating User Guide HTML documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.html -s -f markdown-smart -N --toc -t html5 ${PREDS} ${DMDS} ${POSTDS}
    DEPENDS ${DMDS}
    COMMENT "Generating Developer Guide HTML documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.pdf --from markdown --template eisvogel --listings -N --toc ${AG}
    DEPENDS ${UMDS}
    COMMENT "Generating Advanced PDF documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.html -s -f markdown-smart -N --toc -t html5 ${AG}
    DEPENDS ${DMDS}
    COMMENT "Generating Advanced HTML documentation"
  )

  add_custom_target(
    all_docs ALL
    DEPENDS copy_images ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.pdf ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.html ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.pdf ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.html ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.pdf ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.html
  )

  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.pdf DESTINATION share/doc/pgagroal/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-user-guide.html DESTINATION share/doc/pgagroal/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.pdf DESTINATION share/doc/pgagroal/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-dev-guide.html DESTINATION share/doc/pgagroal/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.pdf DESTINATION share/doc/pgagroal/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-advanced.html DESTINATION share/doc/pgagroal/manual)

endif()

#
# Generate API docs
#
if (DOXYGEN_FOUND)
  add_custom_target(api ALL)

  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_MAN NO)
  set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
  set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.doxygen)

  doxygen_add_docs(doxygen ${PROJECT_SOURCE_DIR}/src/include/)

  if (${DOXYGEN_VERSION} STREQUAL "1.10.0" OR ${DOXYGEN_VERSION} STREQUAL "1.11.0" OR ${DOXYGEN_VERSION} STREQUAL "1.12.0")
    add_custom_command(
      TARGET api
      POST_BUILD
      COMMAND ${DOXYGEN_EXECUTABLE} -q ${DOXYFILE_OUT}
      COMMENT "Generating API documentation"
    )
  else()
    add_custom_command(
      TARGET api
      POST_BUILD
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
      COMMENT "Generating API documentation"
    )
  endif()
endif()
