if (NOT DEFINED VIX_CLI_EXECUTABLE OR NOT DEFINED VIX_P7_FIXTURE_SOURCE OR
    NOT DEFINED VIX_P7_FAILURE_FIXTURE_SOURCE)
  message(FATAL_ERROR "P7 test-migration inputs are required")
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/vix-p7-tests-target")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")
file(COPY "${VIX_P7_FIXTURE_SOURCE}/" DESTINATION "${_root}/project")

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" test --dir "${_root}/project"
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
  COMMAND "${VIX_CLI_EXECUTABLE}" test --dir "${_root}/project" "network.connect[ipv6]+timeout"
  RESULT_VARIABLE _special_result OUTPUT_VARIABLE _special_output ERROR_VARIABLE _special_error)
if (NOT _special_result EQUAL 0)
  message(FATAL_ERROR "P7 literal-name test failed: ${_special_output}${_special_error}")
endif()
string(FIND "${_special_output}${_special_error}" "network.connect[ipv6]+timeout" _special)
if (_special EQUAL -1)
  message(FATAL_ERROR "P7 literal-name registration was not executed: ${_special_output}${_special_error}")
endif()
string(FIND "${_special_output}${_special_error}" "network.connectxiv6timeout" _special_lookalike)
if (NOT _special_lookalike EQUAL -1)
  message(FATAL_ERROR "P7 literal filter selected a regex lookalike: ${_special_output}${_special_error}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" test --dir "${_root}/project" alpha
  RESULT_VARIABLE _filter_result OUTPUT_VARIABLE _filter_output ERROR_VARIABLE _filter_error)
if (NOT _filter_result EQUAL 0)
  message(FATAL_ERROR "P7 literal substring filter failed: ${_filter_output}${_filter_error}")
endif()
string(FIND "${_filter_output}${_filter_error}" "alpha" _filter_alpha)
string(FIND "${_filter_output}${_filter_error}" "beta" _filter_beta)
if (_filter_alpha EQUAL -1 OR NOT _filter_beta EQUAL -1)
  message(FATAL_ERROR "P7 literal substring filter did not select only alpha: ${_filter_output}${_filter_error}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" test --dir "${_root}/project" definitely_missing
  RESULT_VARIABLE _missing_result OUTPUT_VARIABLE _missing_output ERROR_VARIABLE _missing_error)
if (NOT _missing_result EQUAL 0)
  message(FATAL_ERROR "P7 unmatched literal filter failed: ${_missing_output}${_missing_error}")
endif()
string(FIND "${_missing_output}${_missing_error}" "No tests matched filter 'definitely_missing'." _missing)
if (_missing EQUAL -1)
  message(FATAL_ERROR "P7 unmatched filter did not report that no tests matched: ${_missing_output}${_missing_error}")
endif()

file(COPY "${VIX_P7_FAILURE_FIXTURE_SOURCE}/" DESTINATION "${_root}/failure")
execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" test --dir "${_root}/failure" intentional_failure
  RESULT_VARIABLE _failure_result OUTPUT_VARIABLE _failure_output ERROR_VARIABLE _failure_error)
if (_failure_result EQUAL 0)
  message(FATAL_ERROR "P7 failing CTest registration unexpectedly succeeded")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" tests --dir "${_root}/project"
  RESULT_VARIABLE _legacy_result OUTPUT_VARIABLE _legacy_output ERROR_VARIABLE _legacy_error)
if (_legacy_result EQUAL 0)
  message(FATAL_ERROR "Retired vix tests command unexpectedly succeeded")
endif()
string(FIND "${_legacy_output}${_legacy_error}" "unrecognized subcommand \"tests\"" _legacy_rejected)
if (_legacy_rejected EQUAL -1)
  message(FATAL_ERROR "Retired vix tests command did not produce the expected rejection: ${_legacy_output}${_legacy_error}")
endif()
