if (NOT DEFINED VIX_CLI_EXECUTABLE OR NOT DEFINED VIX_P6_FIXTURE_SOURCE)
  message(FATAL_ERROR "P6 run-target migration test inputs are required")
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/vix-p6-run-target")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")
file(COPY "${VIX_P6_FIXTURE_SOURCE}/" DESTINATION "${_root}/project")

function(_p6_expect_failure needle)
  execute_process(
    COMMAND "${VIX_CLI_EXECUTABLE}" run --dir "${_root}/project" ${ARGN}
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if (_result EQUAL 0)
    message(FATAL_ERROR "P6 run unexpectedly succeeded: ${_output}${_error}")
  endif()
  string(FIND "${_output}${_error}" "${needle}" _found)
  if (_found EQUAL -1)
    message(FATAL_ERROR "P6 expected ${needle}: ${_output}${_error}")
  endif()
endfunction()

_p6_expect_failure(ambiguous_target)
_p6_expect_failure(target_not_runnable core)
_p6_expect_failure(target_not_found definitely_missing)

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" run server --dir "${_root}/project"
  RESULT_VARIABLE _server_result OUTPUT_VARIABLE _server_output ERROR_VARIABLE _server_error)
if (NOT _server_result EQUAL 0)
  message(FATAL_ERROR "P6 explicit server run failed: ${_server_output}${_server_error}")
endif()
