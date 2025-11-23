#
# pdflatex Support
#

find_program(PDFLATEX_EXECUTABLE 
  NAMES pdflatex
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pdflatex DEFAULT_MSG
                                  PDFLATEX_EXECUTABLE)

if(PDFLATEX_FOUND)

  # check for kpsewhich
  find_program(KPSEWHICH kpsewhich)
  if(NOT KPSEWHICH)
    set(DOCS FALSE)
    message(STATUS "kpsewhich not found. The generation process will be skipped.")
    return()
  endif()

  # check for packages
  set(check_next TRUE)
  macro(check_latex_package PACKAGE_NAME)
    if(check_next)
      execute_process(
        COMMAND ${KPSEWHICH} ${PACKAGE_NAME}
        OUTPUT_VARIABLE PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(NOT PATH)
        set(DOCS FALSE)
        message(STATUS "${PACKAGE_NAME} not found. The documentation will be skipped.")
        set(check_next FALSE)  # Disable further checks
      else()
        message(STATUS "${PACKAGE_NAME} found at: ${PATH}")
      endif()
    endif()
  endmacro()

  # Use the macro to check for packages
  check_latex_package("footnote.sty")
  check_latex_package("footnotebackref.sty")
  check_latex_package("pagecolor.sty")
  check_latex_package("hardwrap.sty")
  check_latex_package("mdframed.sty")
  check_latex_package("sourcesanspro.sty")
  check_latex_package("ly1enc.def")
  check_latex_package("sourcecodepro.sty")
  check_latex_package("titling.sty")
  check_latex_package("csquotes.sty")
  check_latex_package("zref-abspage.sty")
  check_latex_package("needspace.sty")
  
endif()

mark_as_advanced(PDFLATEX_EXECUTABLE)
