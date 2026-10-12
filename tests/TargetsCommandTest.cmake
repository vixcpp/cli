if (NOT DEFINED VIX_CLI_EXECUTABLE OR NOT DEFINED VIX_P4_FIXTURE_SOURCE)
  message(FATAL_ERROR "P4 target-command test inputs are required")
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/vix-p4-target-command")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")
file(COPY "${VIX_P4_FIXTURE_SOURCE}/" DESTINATION "${_root}/configured")
file(COPY "${VIX_P4_FIXTURE_SOURCE}/" DESTINATION "${_root}/unconfigured")

set(_source "${_root}/configured")
set(_build "${_source}/build-ninja")
file(MAKE_DIRECTORY "${_build}/.cmake/api/v1/query/client-vix")
file(TOUCH "${_build}/.cmake/api/v1/query/client-vix/codemodel-v2")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}" -G Ninja
  RESULT_VARIABLE _configure_result
  OUTPUT_VARIABLE _configure_output
  ERROR_VARIABLE _configure_error
)
if (NOT _configure_result EQUAL 0)
  message(FATAL_ERROR "P4 fixture configure failed: ${_configure_output}${_configure_error}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" targets --dir "${_source}" --build-root "${_build}"
  RESULT_VARIABLE _targets_result
  OUTPUT_VARIABLE _targets_output
  ERROR_VARIABLE _targets_error
)
if (NOT _targets_result EQUAL 0)
  message(FATAL_ERROR "vix targets failed: ${_targets_output}${_targets_error}")
endif()

foreach(_name client core shared objects server generate_assets subcomponent)
  string(FIND "${_targets_output}" "${_name}" _name_position)
  if (_name_position EQUAL -1)
    message(FATAL_ERROR "vix targets omitted ${_name}: ${_targets_output}")
  endif()
endforeach()
string(FIND "${_targets_output}" "::@" _id_position)
if (NOT _id_position EQUAL -1)
  message(FATAL_ERROR "vix targets exposed an internal CMake target ID: ${_targets_output}")
endif()
string(FIND "${_targets_output}" "client" _client_position)
string(FIND "${_targets_output}" "core" _core_position)
if (_client_position GREATER _core_position)
  message(FATAL_ERROR "vix targets did not use stable name ordering: ${_targets_output}")
endif()

execute_process(
  COMMAND "${VIX_CLI_EXECUTABLE}" targets --dir "${_root}/unconfigured" --build-root "${_root}/unconfigured/build"
  RESULT_VARIABLE _unconfigured_result
  OUTPUT_VARIABLE _unconfigured_output
  ERROR_VARIABLE _unconfigured_error
)
if (_unconfigured_result EQUAL 0)
  message(FATAL_ERROR "unconfigured project was silently reported as an empty target list")
endif()
string(FIND "${_unconfigured_output}${_unconfigured_error}" "Configured CMake metadata is unavailable" _metadata_position)
if (_metadata_position EQUAL -1)
  message(FATAL_ERROR "unconfigured failure was unclear: ${_unconfigured_output}${_unconfigured_error}")
endif()
