#
# pandoc Support
#

find_program(PANDOC_EXECUTABLE 
  NAMES pandoc
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pandoc DEFAULT_MSG 
                                    PANDOC_EXECUTABLE)

if(PANDOC_FOUND)
  # check for eisvogel.latex
  execute_process(
    COMMAND ${PANDOC_EXECUTABLE} --version
    OUTPUT_VARIABLE pandoc_output
    RESULT_VARIABLE result
  )
  string(REGEX MATCH "User data directory: ([^\n\r]*)" _ ${pandoc_output})

  if (NOT CMAKE_MATCH_COUNT)
    string(REGEX MATCH "Default user data directory: ([^ ]+)" _ ${pandoc_output})
    set(EISVOGEL_TEMPLATE_PATH "${CMAKE_MATCH_1}/templates/eisvogel.latex")
  else()
    set(EISVOGEL_TEMPLATE_PATH "${CMAKE_MATCH_1}/templates/eisvogel.latex")
  endif()

  if(EXISTS "${EISVOGEL_TEMPLATE_PATH}")
    message(STATUS "Found eisvogel template at ${EISVOGEL_TEMPLATE_PATH}")
  else()
    message(STATUS "eisvogel template not found at ${EISVOGEL_TEMPLATE_PATH}. The generation process will be skipped.")
    set(generation FALSE)
  endif()

endif()

mark_as_advanced(PANDOC_EXECUTABLE)