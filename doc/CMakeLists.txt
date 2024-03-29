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
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_SRC_FILE} ${PGAGROAL_DST_FILE}
  OUTPUTS ${PGAGROAL_DST_FILE}
)

# pgagroal-cli.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_CLI_SRC_FILE} ${PGAGROAL_CLI_DST_FILE}
  OUTPUTS ${PGAGROAL_CLI_DST_FILE}
)

# pgagroal-admin.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_ADMIN_SRC_FILE} ${PGAGROAL_ADMIN_DST_FILE}
  OUTPUTS ${PGAGROAL_ADMIN_DST_FILE}
)

# pgagroal.conf.5
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_CONF_SRC_FILE} ${PGAGROAL_CONF_DST_FILE}
  OUTPUTS ${PGAGROAL_CONF_DST_FILE}
)

# pgagroal_hba.conf.5
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_HBA_CONF_SRC_FILE} ${PGAGROAL_HBA_CONF_DST_FILE}
  OUTPUTS ${PGAGROAL_HBA_CONF_DST_FILE}
)

# pgagroal_databases.conf.5
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_DATABASES_CONF_SRC_FILE} ${PGAGROAL_DATABASES_CONF_DST_FILE}
  OUTPUTS ${PGAGROAL_DATABASES_CONF_DST_FILE}
)

# man pages
add_custom_command(
  TARGET man
  DEPENDS ${PGAGROAL_DST_FILE} ${PGAGROAL_CLI_DST_FILE} ${PGAGROAL_ADMIN_DST_FILE} ${PGAGROAL_CONF_DST_FILE} ${PGAGROAL_HBA_CONF_DST_FILE} ${PGAGROAL_DATABASES_CONF_DST_FILE}
)

#
# Install configuration and documentation
#
install(FILES etc/pgagroal.conf DESTINATION share/doc/pgagroal/etc)
install(FILES etc/pgagroal_hba.conf DESTINATION share/doc/pgagroal/etc)

install(DIRECTORY . DESTINATION share/doc/pgagroal FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "images" EXCLUDE PATTERN "man" EXCLUDE)
install(DIRECTORY images/ DESTINATION share/doc/pgagroal/images FILES_MATCHING PATTERN "*.png")

install(FILES ${PGAGROAL_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGAGROAL_CLI_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGAGROAL_ADMIN_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGAGROAL_CONF_DST_FILE} DESTINATION share/man/man5)
install(FILES ${PGAGROAL_HBA_CONF_DST_FILE} DESTINATION share/man/man5)
install(FILES ${PGAGROAL_DATABASES_CONF_DST_FILE} DESTINATION share/man/man5)
