FILE(TO_NATIVE_PATH "${SCRIPT_SOURCE_DIR}/${SCRIPT_NAME}" INFILE)
FILE(TO_NATIVE_PATH "${SCRIPT_DEST_DIR}/${SCRIPT_NAME}" OUTFILE)

STRING(REPLACE "\"" "" FILE1 "${INFILE}")
STRING(REPLACE "\"" "" FILE2 "${OUTFILE}")

#
# when debugging, this dumps all vars out
#
if (DEBUG_THIS_SCRIPT)
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
endif()

configure_file(
  "${FILE1}"
  "${FILE2}"
  @ONLY
)
