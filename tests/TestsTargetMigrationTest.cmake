if (NOT DEFINED VIX_CLI_EXECUTABLE OR NOT DEFINED VIX_P7_FIXTURE_SOURCE OR
    NOT DEFINED VIX_P7_FAILURE_FIXTURE_SOURCE)
  message(FATAL_ERROR "P7 test-migration inputs are required")
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/vix-p7-tests-target")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")
file(COPY "${VIX_P7_FIXTURE_SOURCE}/" DESTINATION "${_root}/project")

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" tests "${_root}/project"
  RESULT_VARIABLE _all_result OUTPUT_VARIABLE _all_output ERROR_VARIABLE _all_error)
if (NOT _all_result EQUAL 0)
  message(FATAL_ERROR "P7 full CTest suite failed: ${_all_output}${_all_error}")
endif()
string(FIND "${_all_output}${_all_error}" "alpha" _alpha)
string(FIND "${_all_output}${_all_error}" "beta" _beta)
if (_alpha EQUAL -1 OR _beta EQUAL -1)
  message(FATAL_ERROR "P7 suite did not expose both registrations: ${_all_output}${_all_error}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" tests "${_root}/project" --exact-test "network.connect[ipv6]+timeout"
  RESULT_VARIABLE _special_result OUTPUT_VARIABLE _special_output ERROR_VARIABLE _special_error)
if (NOT _special_result EQUAL 0)
  message(FATAL_ERROR "P7 literal-name test failed: ${_special_output}${_special_error}")
endif()
string(FIND "${_special_output}${_special_error}" "network.connect[ipv6]+timeout" _special)
if (_special EQUAL -1)
  message(FATAL_ERROR "P7 literal-name registration was not executed: ${_special_output}${_special_error}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" tests "${_root}/project" --test "^alpha$"
  RESULT_VARIABLE _regex_result OUTPUT_VARIABLE _regex_output ERROR_VARIABLE _regex_error)
if (NOT _regex_result EQUAL 0)
  message(FATAL_ERROR "P7 historical regex filter failed: ${_regex_output}${_regex_error}")
endif()
string(FIND "${_regex_output}${_regex_error}" "alpha" _regex_alpha)
if (_regex_alpha EQUAL -1)
  message(FATAL_ERROR "P7 historical regex filter did not reach CTest: ${_regex_output}${_regex_error}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" tests "${_root}/project" --exact-test definitely_missing
  RESULT_VARIABLE _missing_result OUTPUT_VARIABLE _missing_output ERROR_VARIABLE _missing_error)
if (_missing_result EQUAL 0)
  message(FATAL_ERROR "P7 missing test unexpectedly succeeded")
endif()
string(FIND "${_missing_output}${_missing_error}" "test_not_found" _missing)
if (_missing EQUAL -1)
  message(FATAL_ERROR "P7 missing test did not fail before CTest: ${_missing_output}${_missing_error}")
endif()

file(COPY "${VIX_P7_FAILURE_FIXTURE_SOURCE}/" DESTINATION "${_root}/failure")
execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" tests "${_root}/failure" --exact-test intentional_failure
  RESULT_VARIABLE _failure_result OUTPUT_VARIABLE _failure_output ERROR_VARIABLE _failure_error)
if (_failure_result EQUAL 0)
  message(FATAL_ERROR "P7 failing CTest registration unexpectedly succeeded")
endif()
