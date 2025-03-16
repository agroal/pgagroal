if (check)

  set(SOURCES
    runner.c
    tsclient.c
    testcases/pgagroal_test_1.c
    testcases/pgagroal_test_2.c
  )

  add_executable(pgagroal_test ${SOURCES})
  target_include_directories(pgagroal_test PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_SOURCE_DIR}/test/include)

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgagroal_test Check::check subunit pthread rt m pgagroal)
  elseif(APPLE)
    target_link_libraries(pgagroal_test Check::check m pgagroal)
  else()
    target_link_libraries(pgagroal_test Check::check pthread rt m pgagroal)
  endif()

  add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgagroal_test
    COMMENT "Cleaning up..."
  )
endif()

file(
    COPY
    "${CMAKE_SOURCE_DIR}/test/testsuite.sh"
    DESTINATION "${CMAKE_BINARY_DIR}/"
)