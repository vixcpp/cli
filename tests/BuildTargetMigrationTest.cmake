if (NOT DEFINED VIX_CLI_EXECUTABLE OR NOT DEFINED VIX_P5_FIXTURE_SOURCE)
  message(FATAL_ERROR "P5 build-target migration test inputs are required")
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/vix-p5-build-target")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")

function(_p5_copy_fixture name)
  file(COPY "${VIX_P5_FIXTURE_SOURCE}/" DESTINATION "${_root}/${name}")
endfunction()

function(_p5_expect_success name)
  execute_process(
    COMMAND "${VIX_CLI_EXECUTABLE}" build --dir "${_root}/${name}" ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
  )
  if (NOT _result EQUAL 0)
    message(FATAL_ERROR "P5 build failed for ${name}: ${_output}${_error}")
  endif()
endfunction()

_p5_copy_fixture(server)
_p5_expect_success(server --build-target server)

_p5_copy_fixture(core)
_p5_expect_success(core --build-target core)

_p5_copy_fixture(default)
_p5_expect_success(default)

_p5_copy_fixture(missing)
execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" build --dir "${_root}/missing" --build-target definitely_missing
  RESULT_VARIABLE _missing_result
  OUTPUT_VARIABLE _missing_output
  ERROR_VARIABLE _missing_error
)
if (_missing_result EQUAL 0)
  message(FATAL_ERROR "missing explicit target unexpectedly succeeded")
endif()
string(FIND "${_missing_output}${_missing_error}" "target_not_found" _missing_diagnostic)
if (_missing_diagnostic EQUAL -1)
  message(FATAL_ERROR "missing target was not rejected by normalized selection: ${_missing_output}${_missing_error}")
endif()
